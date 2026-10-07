#include "TurboModuleRegistry.h"

#include "AsyncStorage.h"
#include "BeastWebSocketClient.h"
#include "CurlHttpClient.h"
#include "HostTimerRegistry.h"
#include "I18n.h"
#include "PlatformColor.h"

#include <FBReactNativeSpec/FBReactNativeSpecJSI.h>
#include <ReactCommon/CallInvoker.h>
#include <ReactCommon/CxxTurboModuleUtils.h>
#include <ReactCommon/TurboModule.h>
#include <ReactCommon/TurboModuleBinding.h>
#include <ReactCommon/TurboModuleUtils.h>
#include <array>
#include <cstring>
#include <functional>
#include <iostream>
#include <memory>
#include <optional>
#include <spawn.h>
#include <string>
#include <utility>
#include <vector>

#include <react/coremodules/DeviceInfoModule.h>
#include <react/devsupport/SourceCodeModule.h>
#include <react/io/ImageLoaderModule.h>
#include <react/io/NetworkingModule.h>
#include <react/io/WebSocketModule.h>
#include <react/logging/NativeExceptionsManager.h>
#include <react/nativemodule/cputime/NativeCPUTime.h>
#include <react/nativemodule/defaults/DefaultTurboModules.h>
#include <react/nativemodule/intersectionobserver/NativeIntersectionObserver.h>
#include <react/nativemodule/mutationobserver/NativeMutationObserver.h>
#include <react/renderer/animated/AnimatedModule.h>
#include <react/renderer/animated/NativeAnimatedNodesManagerProvider.h>
#include <react/timing/primitives.h>

extern char** environ;

namespace react_native_linux {

/**
 * `NativeDeviceInfo` answered from this surface's `DimensionsSource` instead of from a platform-wide lookup, which
 * is the bug this module exists not to have (rn-macos#2296).
 *
 * `windowPhysicalPixels` and `screenPhysicalPixels` stay absent: they are the Android half of the payload, and the
 * generated bridging omits an empty `std::optional` rather than sending a null JavaScript would have to defend
 * against.
 */
class LinuxDeviceInfoModule final : public facebook::react::NativeDeviceInfoCxxSpec<LinuxDeviceInfoModule> {
public:
    LinuxDeviceInfoModule(std::shared_ptr<facebook::react::CallInvoker> jsInvoker,
                          std::shared_ptr<DimensionsSource> dimensionsSource)
        : NativeDeviceInfoCxxSpec(std::move(jsInvoker)), dimensionsSource_(std::move(dimensionsSource)) {}

    facebook::react::DeviceInfoConstants getConstants(facebook::jsi::Runtime& /*runtime*/) {
        return facebook::react::DeviceInfoConstants{.Dimensions = toPayload(dimensionsSource_->metrics())};
    }

    void emitDimensionsChange(const DisplayMetrics& metrics) {
        emitDeviceEvent("didUpdateDimensions",
                        [payload = toPayload(metrics), jsInvoker = jsInvoker_](
                            facebook::jsi::Runtime& runtime, std::vector<facebook::jsi::Value>& arguments) {
                            arguments.emplace_back(facebook::react::bridging::toJs(runtime, payload, jsInvoker));
                        });
    }

private:
    static facebook::react::DimensionsPayload toPayload(const DisplayMetrics& metrics) {
        const facebook::react::DisplayMetrics displayMetrics{metrics.width, metrics.height, metrics.scale,
                                                             metrics.fontScale};

        return facebook::react::DimensionsPayload{.window = displayMetrics, .screen = displayMetrics};
    }

    std::shared_ptr<DimensionsSource> dimensionsSource_;
};

/**
 * `NativeAppearance` answered from this host's `AppearanceModel`, which is where the portal signal and the
 * `setColorScheme` override meet (#260). The module owns no policy of its own: `getColorScheme` is the model's
 * resolved answer, `setColorScheme` hands the model a decoded override — `auto` and `unspecified` from
 * `ColorSchemeOverride` are "no override", which is what clearing means — and `appearanceChanged` is emitted from
 * the model's change listener, so a portal signal and an override change reach JavaScript by one path.
 *
 * `addListener` and `removeListeners` are the `RCTEventEmitter` bookkeeping every event-emitting module's spec
 * carries, and are empty here for the same reason they are in upstream's C++ modules: the emit goes through
 * `emitDeviceEvent`, which needs no subscription count to reach `RCTDeviceEventEmitter`. They cannot be factored
 * into a shared base: a CRTP spec resolves `&T::addListener` to a pointer to member of whichever class actually
 * declares it, so an inherited, non-overridden `addListener` binds to the base's type instead of `T`'s and the
 * spec's own `bridging::callFromJs<void>(rt, &T::addListener, ...)` fails to compile.
 */
// jscpd:ignore-start — the CRTP constraint the docblock above states: every event-following module's addListener
// and removeListeners are necessarily this exact pair, declared directly, or the generated spec does not compile.
class LinuxAppearanceModule final : public facebook::react::NativeAppearanceCxxSpec<LinuxAppearanceModule> {
public:
    LinuxAppearanceModule(std::shared_ptr<facebook::react::CallInvoker> jsInvoker,
                          std::shared_ptr<AppearanceModel> appearanceModel)
        : NativeAppearanceCxxSpec(std::move(jsInvoker)), appearanceModel_(std::move(appearanceModel)) {}

    std::string getColorScheme(facebook::jsi::Runtime& /*runtime*/) {
        return std::string(nameOfColorScheme(appearanceModel_->colorScheme()));
    }

    void setColorScheme(facebook::jsi::Runtime& /*runtime*/, const std::string& colorSchemeOverride) {
        appearanceModel_->setColorScheme(colorSchemeFromName(colorSchemeOverride));
    }

    void addListener(facebook::jsi::Runtime& /*runtime*/, const std::string& /*eventName*/) {}

    void removeListeners(facebook::jsi::Runtime& /*runtime*/, double /*count*/) {}

    void emitAppearanceChange(ColorScheme colorScheme) {
        emitDeviceEvent("appearanceChanged",
                        [colorSchemeName = std::string(nameOfColorScheme(colorScheme))](
                            facebook::jsi::Runtime& runtime, std::vector<facebook::jsi::Value>& arguments) {
                            facebook::jsi::Object preferences(runtime);

                            preferences.setProperty(runtime, "colorScheme",
                                                    facebook::jsi::String::createFromUtf8(runtime, colorSchemeName));
                            arguments.emplace_back(std::move(preferences));
                        });
    }

private:
    std::shared_ptr<AppearanceModel> appearanceModel_;
};

// jscpd:ignore-end

/**
 * `NativeLinkingManagerCxxSpec` (#363), backed by `ActivationModel`. `getInitialURL` answers whatever URL last
 * reached this process — this process's own launch `argv` or a later instance's forwarded one — and
 * `emitActivationUrl` is `ActivationModel`'s change listener, so a later activation reaches JavaScript as a `url`
 * device event the same way a portal signal reaches it as `appearanceChanged`.
 *
 * `canOpenURL` and `openURL` have no per-scheme handler registry to consult on this platform, so both go through
 * `xdg-open`: `canOpenURL` is optimistic (`xdg-open` itself decides, and does not expose a dry-run check), and
 * `openURL` spawns it and resolves once the spawn succeeds rather than waiting for the handler to exit, which
 * matches upstream's own fire-and-forget `Linking.openURL` contract. `openSettings` has no desktop equivalent to
 * open and rejects saying so; a real per-application settings surface is out of this module's scope (#23).
 */
// jscpd:ignore-start — see the CRTP note on LinuxAppearanceModule's addListener/removeListeners above: the same
// exact pair, unavoidably, for the same reason.
class LinuxLinkingModule final : public facebook::react::NativeLinkingManagerCxxSpec<LinuxLinkingModule> {
public:
    LinuxLinkingModule(std::shared_ptr<facebook::react::CallInvoker> jsInvoker,
                       std::shared_ptr<ActivationModel> activationModel)
        : NativeLinkingManagerCxxSpec(std::move(jsInvoker)), activationModel_(std::move(activationModel)) {}

    facebook::jsi::Value getInitialURL(facebook::jsi::Runtime& runtime) {
        const std::optional<std::string> url = activationModel_->currentUrl();

        if (!url.has_value()) {
            return facebook::jsi::Value::null();
        }

        return facebook::jsi::String::createFromUtf8(runtime, url.value());
    }

    facebook::react::AsyncPromise<bool> canOpenURL(facebook::jsi::Runtime& runtime, std::string /*url*/) {
        facebook::react::AsyncPromise<bool> promise(runtime, jsInvoker_);
        promise.resolve(true);

        return promise;
    }

    facebook::react::AsyncPromise<> openURL(facebook::jsi::Runtime& runtime, std::string url) {
        facebook::react::AsyncPromise<> promise(runtime, jsInvoker_);
        std::array<char*, 3> spawnArguments{const_cast<char*>("xdg-open"), url.data(), nullptr};
        pid_t spawnedProcessId = 0;
        const int spawnError =
            posix_spawnp(&spawnedProcessId, "xdg-open", nullptr, nullptr, spawnArguments.data(), environ);

        if (spawnError == 0) {
            promise.resolve();
        } else {
            promise.reject(facebook::react::Error(std::strerror(spawnError)));
        }

        return promise;
    }

    facebook::react::AsyncPromise<> openSettings(facebook::jsi::Runtime& runtime) {
        facebook::react::AsyncPromise<> promise(runtime, jsInvoker_);
        promise.reject(facebook::react::Error("openSettings has no desktop equivalent on Linux"));

        return promise;
    }

    void addListener(facebook::jsi::Runtime& /*runtime*/, const std::string& /*eventName*/) {}

    void removeListeners(facebook::jsi::Runtime& /*runtime*/, double /*count*/) {}

    // jscpd:ignore-end

    void emitActivationUrl(const std::string& url) {
        emitDeviceEvent("url", [url](facebook::jsi::Runtime& runtime, std::vector<facebook::jsi::Value>& arguments) {
            facebook::jsi::Object eventPayload(runtime);

            eventPayload.setProperty(runtime, "url", facebook::jsi::String::createFromUtf8(runtime, url));
            arguments.emplace_back(std::move(eventPayload));
        });
    }

private:
    std::shared_ptr<ActivationModel> activationModel_;
};

/**
 * `NativeI18nManager` (#72), over `I18nModel`. `getConstants` answers the direction the choices resolve to now, but
 * React Native's `I18nManager.js` reads it once, so `I18nManager.isRTL` keeps its startup value until the bundle
 * reloads, as upstream's does; the surface itself flips on the next frame, through `WindowSession`.
 */
class LinuxI18nManagerModule final : public facebook::react::NativeI18nManagerCxxSpec<LinuxI18nManagerModule> {
public:
    LinuxI18nManagerModule(std::shared_ptr<facebook::react::CallInvoker> jsInvoker,
                           std::shared_ptr<I18nModel> i18nModel)
        : NativeI18nManagerCxxSpec(std::move(jsInvoker)), i18nModel_(std::move(i18nModel)) {}

    facebook::jsi::Object getConstants(facebook::jsi::Runtime& runtime) {
        facebook::jsi::Object constants(runtime);

        constants.setProperty(runtime, "isRTL", i18nModel_->isRightToLeft());
        constants.setProperty(runtime, "doLeftAndRightSwapInRTL", i18nModel_->doesSwapLeftAndRightInRightToLeft());
        constants.setProperty(runtime, "localeIdentifier",
                              facebook::jsi::String::createFromUtf8(runtime, i18nModel_->localeIdentifier()));

        return constants;
    }

    void allowRTL(facebook::jsi::Runtime& /*runtime*/, bool isAllowed) { i18nModel_->allowRightToLeft(isAllowed); }

    void forceRTL(facebook::jsi::Runtime& /*runtime*/, bool isForced) { i18nModel_->forceRightToLeft(isForced); }

    void swapLeftAndRightInRTL(facebook::jsi::Runtime& /*runtime*/, bool isSwapped) {
        i18nModel_->swapLeftAndRightInRightToLeft(isSwapped);
    }

private:
    std::shared_ptr<I18nModel> i18nModel_;
};

/**
 * `RNAsyncStorage`, the native module `@react-native-async-storage/async-storage` 3.x resolves on any platform that
 * is not web or Windows (#23), over `KeyValueStore`. Written against `TurboModule::methodMap_` rather than a
 * generated spec, because the spec belongs to a package this one does not depend on. `legacy_*` is the v2 surface
 * `getLegacyStorage()` calls, kept in the database named by the empty string; `legacy_multiMerge` is in the spec
 * but no 3.x code path calls it, so it rejects rather than carrying a JSON merge nobody exercises.
 *
 * Every method runs synchronously on the JavaScript thread and settles its promise before returning: the store is
 * one local SQLite file, and a key-value batch costs less than the hop to a worker and back.
 */
class LinuxAsyncStorageModule final : public facebook::react::TurboModule {
public:
    static constexpr std::string_view kModuleName = "RNAsyncStorage";

    LinuxAsyncStorageModule(std::shared_ptr<facebook::react::CallInvoker> jsInvoker,
                            std::shared_ptr<KeyValueStore> store)
        : TurboModule(std::string(kModuleName), std::move(jsInvoker)), store_(std::move(store)) {
        methodMap_["getValues"] = {
            2, [](Runtime& runtime, TurboModule& turboModule, const Value* arguments, size_t count) {
                return settle(
                    runtime,
                    [&](KeyValueStore& store) {
                        return toEntryObjects(runtime, store.get(databaseName(runtime, argument(arguments, count, 0)),
                                                                 toStrings(runtime, argument(arguments, count, 1))));
                    },
                    turboModule);
            }};
        methodMap_["setValues"] = {
            2, [](Runtime& runtime, TurboModule& turboModule, const Value* arguments, size_t count) {
                return settle(
                    runtime,
                    [&](KeyValueStore& store) {
                        const std::vector<KeyValueStore::Entry> entries =
                            fromEntries(runtime, argument(arguments, count, 1));

                        store.set(databaseName(runtime, argument(arguments, count, 0)), entries);

                        return toEntryObjects(runtime, entries);
                    },
                    turboModule);
            }};
        methodMap_["removeValues"] = {
            2, [](Runtime& runtime, TurboModule& turboModule, const Value* arguments, size_t count) {
                return settle(
                    runtime,
                    [&](KeyValueStore& store) {
                        store.remove(databaseName(runtime, argument(arguments, count, 0)),
                                     toStrings(runtime, argument(arguments, count, 1)));

                        return Value::undefined();
                    },
                    turboModule);
            }};
        methodMap_["getKeys"] = {
            1, [](Runtime& runtime, TurboModule& turboModule, const Value* arguments, size_t count) {
                return settle(
                    runtime,
                    [&](KeyValueStore& store) {
                        return toStringArray(runtime, store.keys(databaseName(runtime, argument(arguments, count, 0))));
                    },
                    turboModule);
            }};
        methodMap_["clearStorage"] = {
            1, [](Runtime& runtime, TurboModule& turboModule, const Value* arguments, size_t count) {
                return settle(
                    runtime,
                    [&](KeyValueStore& store) {
                        store.clear(databaseName(runtime, argument(arguments, count, 0)));

                        return Value::undefined();
                    },
                    turboModule);
            }};
        methodMap_["legacy_multiGet"] = {
            1, [](Runtime& runtime, TurboModule& turboModule, const Value* arguments, size_t count) {
                return settle(
                    runtime,
                    [&](KeyValueStore& store) {
                        return toEntryPairs(
                            runtime, store.get(kLegacyDatabase, toStrings(runtime, argument(arguments, count, 0))));
                    },
                    turboModule);
            }};
        methodMap_["legacy_multiSet"] = {
            1, [](Runtime& runtime, TurboModule& turboModule, const Value* arguments, size_t count) {
                return settle(
                    runtime,
                    [&](KeyValueStore& store) {
                        store.set(kLegacyDatabase, fromEntries(runtime, argument(arguments, count, 0)));

                        return Value::undefined();
                    },
                    turboModule);
            }};
        methodMap_["legacy_multiRemove"] = {
            1, [](Runtime& runtime, TurboModule& turboModule, const Value* arguments, size_t count) {
                return settle(
                    runtime,
                    [&](KeyValueStore& store) {
                        store.remove(kLegacyDatabase, toStrings(runtime, argument(arguments, count, 0)));

                        return Value::undefined();
                    },
                    turboModule);
            }};
        methodMap_["legacy_multiMerge"] = {1, [](Runtime& runtime, TurboModule& turboModule, const Value*, size_t) {
                                               return settle(
                                                   runtime,
                                                   [](KeyValueStore&) -> Value {
                                                       throw std::runtime_error(
                                                           "AsyncStorage: legacy_multiMerge is not supported on Linux");
                                                   },
                                                   turboModule);
                                           }};
        methodMap_["legacy_getAllKeys"] = {
            0, [](Runtime& runtime, TurboModule& turboModule, const Value*, size_t) {
                return settle(
                    runtime, [&](KeyValueStore& store) { return toStringArray(runtime, store.keys(kLegacyDatabase)); },
                    turboModule);
            }};
        methodMap_["legacy_clear"] = {0, [](Runtime& runtime, TurboModule& turboModule, const Value*, size_t) {
                                          return settle(
                                              runtime,
                                              [](KeyValueStore& store) {
                                                  store.clear(kLegacyDatabase);

                                                  return Value::undefined();
                                              },
                                              turboModule);
                                      }};
    }

private:
    using Runtime = facebook::jsi::Runtime;
    using Value = facebook::jsi::Value;

    static constexpr const char* kLegacyDatabase = "";

    template <typename Operation>
    static Value settle(Runtime& runtime, Operation&& operation, TurboModule& turboModule) {
        KeyValueStore& store = *static_cast<LinuxAsyncStorageModule&>(turboModule).store_;

        return facebook::react::createPromiseAsJSIValue(
            runtime, [&](Runtime& /*promiseRuntime*/, const std::shared_ptr<facebook::react::Promise>& promise) {
                try {
                    promise->resolve(operation(store));
                } catch (const std::exception& error) {
                    promise->reject(error.what());
                }
            });
    }

    static const Value& argument(const Value* arguments, size_t count, size_t index) {
        if (index >= count) {
            throw std::invalid_argument("AsyncStorage: argument " + std::to_string(index) + " is missing");
        }

        return arguments[index];
    }

    static std::string databaseName(Runtime& runtime, const Value& value) {
        return value.asString(runtime).utf8(runtime);
    }

    static std::vector<std::string> toStrings(Runtime& runtime, const Value& value) {
        const facebook::jsi::Array array = value.asObject(runtime).asArray(runtime);
        std::vector<std::string> strings;

        for (size_t index = 0; index < array.size(runtime); ++index) {
            strings.push_back(array.getValueAtIndex(runtime, index).asString(runtime).utf8(runtime));
        }

        return strings;
    }

    static std::optional<std::string> optionalString(Runtime& runtime, const Value& value) {
        if (value.isString()) {
            return value.getString(runtime).utf8(runtime);
        }

        return std::nullopt;
    }

    static Value valueOf(Runtime& runtime, const std::optional<std::string>& value) {
        if (value.has_value()) {
            return facebook::jsi::String::createFromUtf8(runtime, value.value());
        }

        return Value::null();
    }

    static Value toStringArray(Runtime& runtime, const std::vector<std::string>& strings) {
        facebook::jsi::Array array(runtime, strings.size());

        for (size_t index = 0; index < strings.size(); ++index) {
            array.setValueAtIndex(runtime, index, facebook::jsi::String::createFromUtf8(runtime, strings[index]));
        }

        return array;
    }

    /** `{ key, value }` objects from `setValues`, `[key, value]` pairs from `legacy_multiSet`. */
    static std::vector<KeyValueStore::Entry> fromEntries(Runtime& runtime, const Value& value) {
        const facebook::jsi::Array array = value.asObject(runtime).asArray(runtime);
        std::vector<KeyValueStore::Entry> entries;

        for (size_t index = 0; index < array.size(runtime); ++index) {
            const facebook::jsi::Object element = array.getValueAtIndex(runtime, index).asObject(runtime);

            if (element.isArray(runtime)) {
                const facebook::jsi::Array pair = element.getArray(runtime);

                entries.emplace_back(pair.getValueAtIndex(runtime, 0).asString(runtime).utf8(runtime),
                                     optionalString(runtime, pair.getValueAtIndex(runtime, 1)));
            } else {
                entries.emplace_back(element.getProperty(runtime, "key").asString(runtime).utf8(runtime),
                                     optionalString(runtime, element.getProperty(runtime, "value")));
            }
        }

        return entries;
    }

    static Value toEntryObjects(Runtime& runtime, const std::vector<KeyValueStore::Entry>& entries) {
        facebook::jsi::Array array(runtime, entries.size());

        for (size_t index = 0; index < entries.size(); ++index) {
            facebook::jsi::Object entry(runtime);

            entry.setProperty(runtime, "key", facebook::jsi::String::createFromUtf8(runtime, entries[index].first));
            entry.setProperty(runtime, "value", valueOf(runtime, entries[index].second));
            array.setValueAtIndex(runtime, index, std::move(entry));
        }

        return array;
    }

    static Value toEntryPairs(Runtime& runtime, const std::vector<KeyValueStore::Entry>& entries) {
        facebook::jsi::Array array(runtime, entries.size());

        for (size_t index = 0; index < entries.size(); ++index) {
            array.setValueAtIndex(runtime, index,
                                  facebook::jsi::Array::createWithElements(
                                      runtime, facebook::jsi::String::createFromUtf8(runtime, entries[index].first),
                                      valueOf(runtime, entries[index].second)));
        }

        return array;
    }

    std::shared_ptr<KeyValueStore> store_;
};

/**
 * `NativeFantomCxx` (#210, #423): the methods upstream Fantom's in-runtime test harness
 * (`private/react-native-fantom/runtime/setup.js`) and its itests call. `reportTestSuiteResultsJSON` prints the
 * suite's results on one `[fantom]` line for `scripts/fantom.ts` to read, and `validateEmptyMessageQueue` asks
 * nothing of a host whose queues drain on their own. `flushMessageQueue` runs every task queued on the itest
 * runtime's `StubMessageQueue`, re-entrantly from inside the call, which is what `Fantom.runTask` and the work loop
 * stand on, and `setTimerMockEnabled`, `advanceTimers`, `runAllTimers` and `getPendingTimerCount` are Fantom's timer
 * mock over `HostTimerRegistry`'s mock mode, and `startSurface` and `stopSurface` are `Fantom.createRoot`'s surfaces
 * on the Fabric host; all seven exist only where the registry was given an itest run's `FantomRunControls`.
 * `forceHighResTimeStamp` pins `HighResTimeStamp::now()` for the whole process, or unpins it given no number, exactly
 * as upstream's tester does; the hook exists only in a debug build, so an optimised one throws upstream's own message
 * instead. Every other method of the spec drives a surface, an event or a timer mock this runner does not provide yet,
 * so it is absent and a test that calls one fails naming it.
 */
/**
 * `DevSettings` for a `dev=true` bundle, whose startup requires the module (#79). Every member is a no-op: reload
 * and Fast Refresh arrive with #81, and this platform has no dev menu, element inspector or debugger launcher to
 * toggle. Upstream's C++ `DevSettingsModule` is not used because it links `DevServerHelper`, which needs OpenSSL
 * and the inspector for its one call, `openDebugger`.
 */
class LinuxDevSettingsModule final : public facebook::react::NativeDevSettingsCxxSpec<LinuxDevSettingsModule> {
public:
    explicit LinuxDevSettingsModule(std::shared_ptr<facebook::react::CallInvoker> jsInvoker)
        : NativeDevSettingsCxxSpec(std::move(jsInvoker)) {}

    void reload(facebook::jsi::Runtime& /*runtime*/) {}

    void reloadWithReason(facebook::jsi::Runtime& /*runtime*/, const std::string& /*reason*/) {}

    void onFastRefresh(facebook::jsi::Runtime& /*runtime*/) {}

    void setHotLoadingEnabled(facebook::jsi::Runtime& /*runtime*/, bool /*isHotLoadingEnabled*/) {}

    void setIsDebuggingRemotely(facebook::jsi::Runtime& /*runtime*/, bool /*isDebuggingRemotelyEnabled*/) {}

    void setProfilingEnabled(facebook::jsi::Runtime& /*runtime*/, bool /*isProfilingEnabled*/) {}

    void toggleElementInspector(facebook::jsi::Runtime& /*runtime*/) {}

    void addMenuItem(facebook::jsi::Runtime& /*runtime*/, const std::string& /*title*/) {}

    void setIsShakeToShowDevMenuEnabled(facebook::jsi::Runtime& /*runtime*/, bool /*enabled*/) {}

    void openDebugger(facebook::jsi::Runtime& /*runtime*/) {}

    void addListener(facebook::jsi::Runtime& /*runtime*/, const std::string& /*eventName*/) {}

    void removeListeners(facebook::jsi::Runtime& /*runtime*/, double /*count*/) {}
};

class LinuxFantomModule final : public facebook::react::TurboModule {
public:
    static constexpr std::string_view kModuleName = "NativeFantomCxx";

    LinuxFantomModule(std::shared_ptr<facebook::react::CallInvoker> jsInvoker,
                      std::optional<FantomRunControls> fantomRunControls)
        : TurboModule(std::string(kModuleName), std::move(jsInvoker)),
          fantomRunControls_(std::move(fantomRunControls)) {
        methodMap_["reportTestSuiteResultsJSON"] = {1, [](facebook::jsi::Runtime& runtime, TurboModule& /*turboModule*/,
                                                          const facebook::jsi::Value* arguments, size_t count) {
                                                        if (count > 0 && arguments[0].isString()) {
                                                            std::cout << "[fantom] "
                                                                      << arguments[0].getString(runtime).utf8(runtime)
                                                                      << std::endl;
                                                        }

                                                        return facebook::jsi::Value::undefined();
                                                    }};
        methodMap_["validateEmptyMessageQueue"] = {0,
                                                   [](facebook::jsi::Runtime& /*runtime*/, TurboModule& /*turboModule*/,
                                                      const facebook::jsi::Value* /*arguments*/,
                                                      size_t /*count*/) { return facebook::jsi::Value::undefined(); }};
        methodMap_["forceHighResTimeStamp"] = {1, &forceHighResTimeStamp};

        if (fantomRunControls_.has_value()) {
            methodMap_["flushMessageQueue"] = {0, &flushMessageQueue};
            methodMap_["setTimerMockEnabled"] = {1, &setTimerMockEnabled};
            methodMap_["advanceTimers"] = {1, &advanceTimers};
            methodMap_["runAllTimers"] = {0, &runAllTimers};
            methodMap_["getPendingTimerCount"] = {0, &getPendingTimerCount};
            methodMap_["startSurface"] = {5, &startSurface};
            methodMap_["stopSurface"] = {1, &stopSurface};
        }
    }

private:
    static FantomRunControls& controlsOf(TurboModule& turboModule) {
        return *static_cast<LinuxFantomModule&>(turboModule).fantomRunControls_;
    }

    static facebook::jsi::Value flushMessageQueue(facebook::jsi::Runtime& /*runtime*/, TurboModule& turboModule,
                                                  const facebook::jsi::Value* /*arguments*/, size_t /*count*/) {
        controlsOf(turboModule).flushMessageQueue();

        return facebook::jsi::Value::undefined();
    }

    static facebook::jsi::Value setTimerMockEnabled(facebook::jsi::Runtime& /*runtime*/, TurboModule& turboModule,
                                                    const facebook::jsi::Value* arguments, size_t count) {
        controlsOf(turboModule)
            .timerRegistry->setMockEnabled(count > 0 && arguments[0].isBool() && arguments[0].getBool());

        return facebook::jsi::Value::undefined();
    }

    static facebook::jsi::Value advanceTimers(facebook::jsi::Runtime& /*runtime*/, TurboModule& turboModule,
                                              const facebook::jsi::Value* arguments, size_t count) {
        controlsOf(turboModule)
            .timerRegistry->advanceTimersByTime(count > 0 && arguments[0].isNumber() ? arguments[0].getNumber() : 0.0);

        return facebook::jsi::Value::undefined();
    }

    static facebook::jsi::Value runAllTimers(facebook::jsi::Runtime& /*runtime*/, TurboModule& turboModule,
                                             const facebook::jsi::Value* /*arguments*/, size_t /*count*/) {
        controlsOf(turboModule).timerRegistry->runAllTimers();

        return facebook::jsi::Value::undefined();
    }

    /**
     * `startSurface(viewportWidth, viewportHeight, devicePixelRatio, viewportOffsetX, viewportOffsetY)`, answering
     * the new surface's id. Ids start at 11 and step by 10, as upstream's tester's do, which keeps them clear of the
     * Fabric host's own surface 1. The viewport offset has no consumer on this host and is not applied.
     */
    static facebook::jsi::Value startSurface(facebook::jsi::Runtime& /*runtime*/, TurboModule& turboModule,
                                             const facebook::jsi::Value* arguments, size_t count) {
        LinuxFantomModule& module = static_cast<LinuxFantomModule&>(turboModule);
        const facebook::react::SurfaceId surfaceId = module.nextSurfaceId_;
        const facebook::react::Size viewport{
            .width = static_cast<facebook::react::Float>(numberAt(arguments, count, 0)),
            .height = static_cast<facebook::react::Float>(numberAt(arguments, count, 1))};

        module.nextSurfaceId_ += kSurfaceIdStep;
        module.fantomRunControls_->startSurface(surfaceId, viewport,
                                                static_cast<facebook::react::Float>(numberAt(arguments, count, 2)));

        return {surfaceId};
    }

    static facebook::jsi::Value stopSurface(facebook::jsi::Runtime& /*runtime*/, TurboModule& turboModule,
                                            const facebook::jsi::Value* arguments, size_t count) {
        controlsOf(turboModule).stopSurface(static_cast<facebook::react::SurfaceId>(numberAt(arguments, count, 0)));

        return facebook::jsi::Value::undefined();
    }

    static double numberAt(const facebook::jsi::Value* arguments, size_t count, size_t index) {
        return index < count && arguments[index].isNumber() ? arguments[index].getNumber() : 0.0;
    }

    static facebook::jsi::Value getPendingTimerCount(facebook::jsi::Runtime& /*runtime*/, TurboModule& turboModule,
                                                     const facebook::jsi::Value* /*arguments*/, size_t /*count*/) {
        return {static_cast<double>(controlsOf(turboModule).timerRegistry->pendingMockTimerCount())};
    }

    static facebook::jsi::Value forceHighResTimeStamp(facebook::jsi::Runtime& runtime, TurboModule& /*turboModule*/,
                                                      const facebook::jsi::Value* arguments, size_t count) {
#ifdef REACT_NATIVE_DEBUG
        static_cast<void>(runtime);

        if (count > 0 && arguments[0].isNumber()) {
            const facebook::react::HighResTimeStamp now =
                facebook::react::HighResTimeStamp::fromDOMHighResTimeStamp(arguments[0].getNumber());

            facebook::react::HighResTimeStamp::setTimeStampProviderForTesting(
                [now]() { return now.toChronoSteadyClockTimePoint(); });
        } else {
            facebook::react::HighResTimeStamp::setTimeStampProviderForTesting(nullptr);
        }

        return facebook::jsi::Value::undefined();
#else
        static_cast<void>(arguments);
        static_cast<void>(count);

        throw facebook::jsi::JSError(runtime, "Mocking timers is not supported in optimized builds");
#endif
    }

    static constexpr facebook::react::SurfaceId kSurfaceIdStep = 10;

    std::optional<FantomRunControls> fantomRunControls_;
    facebook::react::SurfaceId nextSurfaceId_{11};
};

/**
 * `PlatformColor('name')`, as the one host function a JavaScript `PlatformColorValueTypes.linux.js` needs.
 *
 * It is a global rather than a module method because `PlatformColor` has no TurboModule spec on any platform:
 * iOS and macOS resolve a semantic name inside their colour parser, and a colour has to be resolvable
 * synchronously from `processColor`, which is not a call site an asynchronous module method can serve. An
 * unrecognised name answers `undefined` rather than a colour, so the JavaScript side throws a named error instead
 * of putting an undefined colour into a prop, which is rn-macos#413.
 *
 * The scheme is read on every call, so there is no resolved value anywhere that could go stale: the re-render an
 * `appearanceChanged` triggers resolves the same names again and gets the new scheme's colours.
 */
void installPlatformColorBinding(facebook::jsi::Runtime& runtime, std::shared_ptr<AppearanceModel> appearanceModel) {
    constexpr unsigned int kPlatformColorArgumentCount = 1;

    runtime.global().setProperty(
        runtime, "__rnlPlatformColor",
        facebook::jsi::Function::createFromHostFunction(
            runtime, facebook::jsi::PropNameID::forAscii(runtime, "__rnlPlatformColor"), kPlatformColorArgumentCount,
            [appearanceModel = std::move(appearanceModel)](
                facebook::jsi::Runtime& hostRuntime, const facebook::jsi::Value& /*thisValue*/,
                const facebook::jsi::Value* arguments, size_t count) -> facebook::jsi::Value {
                if (count < kPlatformColorArgumentCount || !arguments[0].isString()) {
                    return facebook::jsi::Value::undefined();
                }

                const std::optional<int32_t> resolved = platformColor(
                    arguments[0].getString(hostRuntime).utf8(hostRuntime), appearanceModel->colorScheme());

                if (!resolved.has_value()) {
                    return facebook::jsi::Value::undefined();
                }

                return facebook::jsi::Value(resolved.value());
            }));
}

TurboModuleRegistry::TurboModuleRegistry(
    std::shared_ptr<facebook::react::CallInvoker> jsInvoker,
    std::shared_ptr<facebook::react::NativeAnimatedNodesManagerProvider> animatedNodesManagerProvider,
    facebook::react::JsErrorHandler::OnJsError onJsError, std::optional<FantomRunControls> fantomRunControls)
    : jsInvoker_(jsInvoker), dimensionsSource_(std::make_shared<DimensionsSource>()),
      deviceInfoModule_(std::make_shared<LinuxDeviceInfoModule>(jsInvoker, dimensionsSource_)),
      appearanceModel_(std::make_shared<AppearanceModel>(kFallbackColorScheme)),
      appearanceModule_(std::make_shared<LinuxAppearanceModule>(jsInvoker, appearanceModel_)),
      activationModel_(std::make_shared<ActivationModel>()),
      linkingModule_(std::make_shared<LinuxLinkingModule>(jsInvoker, activationModel_)),
      keyValueStore_(std::make_shared<KeyValueStore>()),
      i18nModel_(std::make_shared<I18nModel>(localeFromEnvironment(), keyValueStore_)),
      bundleUrl_(std::make_shared<std::string>()) {
    appearanceModel_->setChangeListener([appearanceModule = appearanceModule_.get()](ColorScheme colorScheme) {
        appearanceModule->emitAppearanceChange(colorScheme);
    });
    activationModel_->setChangeListener(
        [linkingModule = linkingModule_.get()](const std::string& url) { linkingModule->emitActivationUrl(url); });
    moduleFactories_.emplace(LinuxDeviceInfoModule::kModuleName,
                             [deviceInfoModule = deviceInfoModule_]() { return deviceInfoModule; });
    moduleFactories_.emplace(LinuxAppearanceModule::kModuleName,
                             [appearanceModule = appearanceModule_]() { return appearanceModule; });
    moduleFactories_.emplace(LinuxLinkingModule::kModuleName,
                             [linkingModule = linkingModule_]() { return linkingModule; });
    moduleFactories_.emplace(LinuxAsyncStorageModule::kModuleName, [jsInvoker, keyValueStore = keyValueStore_]() {
        return std::make_shared<LinuxAsyncStorageModule>(jsInvoker, keyValueStore);
    });
    moduleFactories_.emplace(LinuxI18nManagerModule::kModuleName, [jsInvoker, i18nModel = i18nModel_]() {
        return std::make_shared<LinuxI18nManagerModule>(jsInvoker, i18nModel);
    });
    moduleFactories_.emplace(LinuxFantomModule::kModuleName,
                             [jsInvoker, fantomRunControls = std::move(fantomRunControls)]() {
                                 return std::make_shared<LinuxFantomModule>(jsInvoker, fantomRunControls);
                             });
    // Upstream's own CPU-time module, which Fantom's test runtime and the web-performance itests read.
    moduleFactories_.emplace(facebook::react::NativeCPUTime::kModuleName,
                             [jsInvoker]() { return std::make_shared<facebook::react::NativeCPUTime>(jsInvoker); });
    // #79: the modules a `dev=true` bundle requires at startup. `SourceCode` answers the bundle URL, which is
    // how the bundle finds its dev server.
    moduleFactories_.emplace(facebook::react::SourceCodeModule::kModuleName, [jsInvoker, bundleUrl = bundleUrl_]() {
        return std::make_shared<facebook::react::SourceCodeModule>(jsInvoker, *bundleUrl);
    });
    moduleFactories_.emplace(LinuxDevSettingsModule::kModuleName,
                             [jsInvoker]() { return std::make_shared<LinuxDevSettingsModule>(jsInvoker); });
    // `Image.android.js`, which LogBox loads in a `dev=true` bundle, requires `ImageLoader` as it is imported. With no
    // `IImageLoader` behind it, upstream's module rejects `getSize` and `prefetch`, which is the honest answer here.
    moduleFactories_.emplace(facebook::react::ImageLoaderModule::kModuleName,
                             [jsInvoker]() { return std::make_shared<facebook::react::ImageLoaderModule>(jsInvoker); });
    // The two observer modules, registered whatever the flags say, as upstream's own C++ host registers them
    // (ReactCxxPlatform's `ReactCxxTurboModuleProvider`). `DefaultTurboModules` serves them only behind
    // `enableIntersectionObserverByDefault` and `enableMutationObserverByDefault`. JavaScript still installs the
    // `IntersectionObserver` and `MutationObserver` globals only behind those flags.
    moduleFactories_.emplace(facebook::react::NativeIntersectionObserver::kModuleName, [jsInvoker]() {
        return std::make_shared<facebook::react::NativeIntersectionObserver>(jsInvoker);
    });
    moduleFactories_.emplace(facebook::react::NativeMutationObserver::kModuleName, [jsInvoker]() {
        return std::make_shared<facebook::react::NativeMutationObserver>(jsInvoker);
    });
    // #79: `fetch` and `XMLHttpRequest` reach upstream's C++ Networking module, which this platform only supplies
    // the HTTP client for.
    moduleFactories_.emplace(facebook::react::NetworkingModule::kModuleName, [jsInvoker]() {
        return std::make_shared<facebook::react::NetworkingModule>(jsInvoker,
                                                                   []() { return std::make_unique<CurlHttpClient>(); });
    });
    // #79: `WebSocket` reaches upstream's C++ WebSocket module, which this platform supplies the client for.
    moduleFactories_.emplace(facebook::react::WebSocketModule::kModuleName, [jsInvoker]() {
        return std::make_shared<facebook::react::WebSocketModule>(
            jsInvoker, []() { return std::make_unique<BeastWebSocketClient>(); });
    });
    // #22: React Native's ExceptionsManager, upstream's C++ one, reporting through the host's own error handler —
    // the same one a fatal error reaches through JsErrorHandler, so both paths print and record alike.
    moduleFactories_.emplace(
        facebook::react::NativeExceptionsManager::kModuleName, [jsInvoker, onJsError = std::move(onJsError)]() {
            return std::make_shared<facebook::react::NativeExceptionsManager>(onJsError, jsInvoker);
        });

    moduleFactories_.emplace(facebook::react::AnimatedModule::kModuleName,
                             [jsInvoker, animatedNodesManagerProvider = std::move(animatedNodesManagerProvider)]() {
                                 return std::make_shared<facebook::react::AnimatedModule>(jsInvoker,
                                                                                          animatedNodesManagerProvider);
                             });

    // An autolinked library's C++ TurboModules, registered by the generated rnl_autolinking.cpp (#147), last so that
    // a name this platform already serves keeps ours. The map's keys live for the whole process, so viewing is safe.
    for (const auto& [name, moduleProvider] : facebook::react::globalExportedCxxTurboModuleMap()) {
        moduleFactories_.emplace(name, [moduleProvider, jsInvoker]() { return moduleProvider(jsInvoker); });
    }
}

DimensionsSource& TurboModuleRegistry::dimensions() noexcept { return *dimensionsSource_; }

AppearanceModel& TurboModuleRegistry::appearance() noexcept { return *appearanceModel_; }

ActivationModel& TurboModuleRegistry::activation() noexcept { return *activationModel_; }

void TurboModuleRegistry::setBundleUrl(const std::string& bundleUrl) { *bundleUrl_ = bundleUrl; }

KeyValueStore& TurboModuleRegistry::keyValueStore() noexcept { return *keyValueStore_; }

I18nModel& TurboModuleRegistry::i18n() noexcept { return *i18nModel_; }

void TurboModuleRegistry::install(facebook::jsi::Runtime& runtime) {
    installPlatformColorBinding(runtime, appearanceModel_);
    facebook::react::TurboModuleBinding::install(
        runtime,
        [moduleFactories = moduleFactories_,
         jsInvoker = jsInvoker_](facebook::jsi::Runtime& /*runtime*/,
                                 const std::string& name) -> std::shared_ptr<facebook::react::TurboModule> {
            const auto moduleFactory = moduleFactories.find(name);

            if (moduleFactory == moduleFactories.end()) {
                // #22: everything React Native's own JavaScript asks for that this platform does not serve itself —
                // feature flags, microtasks, DOM, the observers — is upstream's default C++ module, unchanged.
                return facebook::react::DefaultTurboModules::getTurboModule(name, jsInvoker);
            }

            return moduleFactory->second();
        });
}

void TurboModuleRegistry::publishPendingDimensions() {
    const std::optional<DisplayMetrics> change = dimensionsSource_->takeChangeIfAny();

    if (change.has_value()) {
        deviceInfoModule_->emitDimensionsChange(change.value());
    }
}

} // namespace react_native_linux
