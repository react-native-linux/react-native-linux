#include "TurboModuleRegistry.h"

#include "AsyncStorage.h"
#include "CurlHttpClient.h"
#include "PlatformColor.h"

#include <FBReactNativeSpec/FBReactNativeSpecJSI.h>
#include <ReactCommon/CallInvoker.h>
#include <ReactCommon/CxxTurboModuleUtils.h>
#include <ReactCommon/TurboModule.h>
#include <ReactCommon/TurboModuleBinding.h>
#include <ReactCommon/TurboModuleUtils.h>
#include <array>
#include <cstring>
#include <iostream>
#include <memory>
#include <optional>
#include <spawn.h>
#include <string>
#include <utility>
#include <vector>

#include <react/coremodules/DeviceInfoModule.h>
#include <react/io/NetworkingModule.h>
#include <react/logging/NativeExceptionsManager.h>
#include <react/nativemodule/cputime/NativeCPUTime.h>
#include <react/nativemodule/defaults/DefaultTurboModules.h>
#include <react/renderer/animated/AnimatedModule.h>
#include <react/renderer/animated/NativeAnimatedNodesManagerProvider.h>

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
 * `NativeFantomCxx` (#210, #423): the two methods upstream Fantom's in-runtime test harness
 * (`private/react-native-fantom/runtime/setup.js`) calls in a plain itest. `reportTestSuiteResultsJSON` prints the
 * suite's results on one `[fantom]` line for `scripts/fantom.ts` to read, and `validateEmptyMessageQueue` asks
 * nothing of a host whose queues drain on their own. Every other method of the spec drives a surface, an event or
 * a timer mock this runner does not provide yet, so it is absent and a test that calls one fails naming it.
 */
class LinuxFantomModule final : public facebook::react::TurboModule {
public:
    static constexpr std::string_view kModuleName = "NativeFantomCxx";

    explicit LinuxFantomModule(std::shared_ptr<facebook::react::CallInvoker> jsInvoker)
        : TurboModule(std::string(kModuleName), std::move(jsInvoker)) {
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
    }
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
    facebook::react::JsErrorHandler::OnJsError onJsError)
    : jsInvoker_(jsInvoker), dimensionsSource_(std::make_shared<DimensionsSource>()),
      deviceInfoModule_(std::make_shared<LinuxDeviceInfoModule>(jsInvoker, dimensionsSource_)),
      appearanceModel_(std::make_shared<AppearanceModel>(kFallbackColorScheme)),
      appearanceModule_(std::make_shared<LinuxAppearanceModule>(jsInvoker, appearanceModel_)),
      activationModel_(std::make_shared<ActivationModel>()),
      linkingModule_(std::make_shared<LinuxLinkingModule>(jsInvoker, activationModel_)),
      keyValueStore_(std::make_shared<KeyValueStore>()) {
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
    moduleFactories_.emplace(LinuxFantomModule::kModuleName,
                             [jsInvoker]() { return std::make_shared<LinuxFantomModule>(jsInvoker); });
    // Upstream's own CPU-time module, which Fantom's test runtime and the web-performance itests read.
    moduleFactories_.emplace(facebook::react::NativeCPUTime::kModuleName,
                             [jsInvoker]() { return std::make_shared<facebook::react::NativeCPUTime>(jsInvoker); });
    // #79: `fetch` and `XMLHttpRequest` reach upstream's C++ Networking module, which this platform only supplies
    // the HTTP client for.
    moduleFactories_.emplace(facebook::react::NetworkingModule::kModuleName, [jsInvoker]() {
        return std::make_shared<facebook::react::NetworkingModule>(jsInvoker,
                                                                   []() { return std::make_unique<CurlHttpClient>(); });
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

KeyValueStore& TurboModuleRegistry::keyValueStore() noexcept { return *keyValueStore_; }

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
