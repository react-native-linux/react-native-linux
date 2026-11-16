#include "ExpoSecureStore.h"

#include <ReactCommon/CallInvoker.h>
#include <ReactCommon/TurboModule.h>
#include <cstddef>
#include <exception>
#include <functional>
#include <future>
#include <jsi/jsi.h>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>

#include <react/bridging/Bridging.h>

namespace react_native_linux {

namespace {

using facebook::jsi::Runtime;
using facebook::jsi::Value;
using facebook::react::TurboModule;

/** The `keychainService` upstream's iOS module uses when the option is absent. */
constexpr char kDefaultService[] = "app";

/**
 * `ExpoSecureStore`, the native module upstream expo-secure-store's JavaScript requires (#270), over `SecureStore`:
 * the desktop keyring through the Secret Service, or the encrypted file when the session has none. Its surface is
 * upstream's iOS module's: the seven `keychainAccessible` constants, the asynchronous and synchronous get and set, the
 * asynchronous delete, and `canUseBiometricAuthentication`, which is `false`. `keychainService` names the store a
 * value is in, defaulting to iOS's `app`; `keychainAccessible`, `accessGroup`, `authenticationPrompt` and
 * `requireConfirmation` have nothing to act on here and are ignored. `requireAuthentication` asks for a biometric
 * gate this platform does not have, so a set that asks for it fails rather than storing the value without it. A
 * failure rejects with the message upstream's Android module gives for it.
 *
 * Threading contract: arguments are read on the JavaScript thread, and every backend call runs on `SecureStore`'s
 * worker. An asynchronous method settles its `AsyncPromise` back on the JavaScript thread through the call invoker; a
 * synchronous one blocks the JavaScript thread until the worker has run it, after every call queued before it. The
 * module holds the store by reference because `TurboModuleRegistry` owns it and outlives every JavaScript call.
 */
class LinuxSecureStoreModule final : public TurboModule {
public:
    LinuxSecureStoreModule(std::shared_ptr<facebook::react::CallInvoker> jsInvoker, SecureStore& store)
        : TurboModule("ExpoSecureStore", std::move(jsInvoker)), store_(store) {
        methodMap_["getConstants"] = {
            0, [](Runtime& runtime, TurboModule&, const Value*, size_t) { return constants(runtime); }};
        methodMap_["getValueWithKeyAsync"] = {
            2, [](Runtime& runtime, TurboModule& turboModule, const Value* arguments, size_t count) {
                auto& module = static_cast<LinuxSecureStoreModule&>(turboModule);

                return module.schedule(runtime, reading(locationOf(runtime, arguments, count, 0)));
            }};
        methodMap_["getValueWithKeySync"] = {
            2, [](Runtime& runtime, TurboModule& turboModule, const Value* arguments, size_t count) {
                const std::optional<std::string> value = static_cast<LinuxSecureStoreModule&>(turboModule)
                                                             .await(reading(locationOf(runtime, arguments, count, 0)));

                return value.has_value() ? Value(facebook::jsi::String::createFromUtf8(runtime, value.value()))
                                         : Value::null();
            }};
        methodMap_["setValueWithKeyAsync"] = {
            3, [](Runtime& runtime, TurboModule& turboModule, const Value* arguments, size_t count) {
                auto& module = static_cast<LinuxSecureStoreModule&>(turboModule);

                return module.schedule(runtime, writing(runtime, arguments, count));
            }};
        methodMap_["setValueWithKeySync"] = {
            3, [](Runtime& runtime, TurboModule& turboModule, const Value* arguments, size_t count) {
                return Value(
                    static_cast<LinuxSecureStoreModule&>(turboModule).await(writing(runtime, arguments, count)));
            }};
        methodMap_["deleteValueWithKeyAsync"] = {
            2, [](Runtime& runtime, TurboModule& turboModule, const Value* arguments, size_t count) {
                auto& module = static_cast<LinuxSecureStoreModule&>(turboModule);

                return module.schedule(runtime, deleting(locationOf(runtime, arguments, count, 0)));
            }};
        methodMap_["canUseBiometricAuthentication"] = {
            0, [](Runtime&, TurboModule&, const Value*, size_t) { return Value(false); }};
    }

private:
    template <typename Work> static auto describingFailure(const std::string& failure, Work work) {
        try {
            return work();
        } catch (const std::exception& error) {
            throw std::runtime_error(failure + error.what());
        }
    }

    static Value constants(Runtime& runtime) {
        facebook::jsi::Object constants(runtime);
        int value = 0;

        for (const char* name : {"AFTER_FIRST_UNLOCK", "AFTER_FIRST_UNLOCK_THIS_DEVICE_ONLY", "ALWAYS",
                                 "WHEN_PASSCODE_SET_THIS_DEVICE_ONLY", "ALWAYS_THIS_DEVICE_ONLY", "WHEN_UNLOCKED",
                                 "WHEN_UNLOCKED_THIS_DEVICE_ONLY"}) {
            constants.setProperty(runtime, name, value++);
        }

        return constants;
    }

    static const Value& argument(const Value* arguments, size_t count, size_t index) {
        if (index >= count) {
            throw std::invalid_argument("ExpoSecureStore: argument " + std::to_string(index) + " is missing");
        }

        return arguments[index];
    }

    static std::optional<facebook::jsi::Object> optionsOf(Runtime& runtime, const Value* arguments, size_t count,
                                                          size_t index) {
        if (index >= count || !arguments[index].isObject()) {
            return std::nullopt;
        }

        return arguments[index].getObject(runtime);
    }

    /** `(key, options)` from `index`, as every method but the sets takes them. */
    static SecretLocation locationOf(Runtime& runtime, const Value* arguments, size_t count, size_t index) {
        SecretLocation location{.service = kDefaultService,
                                .key = argument(arguments, count, index).asString(runtime).utf8(runtime)};
        const std::optional<facebook::jsi::Object> options = optionsOf(runtime, arguments, count, index + 1);

        if (options.has_value()) {
            const Value service = options->getProperty(runtime, "keychainService");

            if (service.isString()) {
                location.service = service.getString(runtime).utf8(runtime);
            }
        }

        return location;
    }

    static std::function<std::optional<std::string>(SecureStore&)> reading(SecretLocation location) {
        const std::string failure = "Could not decrypt the value for key '" + location.key + "' under keychain '" +
                                    location.service + "'. Caused by: ";

        return [location = std::move(location), failure](SecureStore& store) {
            return describingFailure(failure, [&] { return store.backend().read(location); });
        };
    }

    static std::function<bool(SecureStore&)> deleting(SecretLocation location) {
        const std::string failure = "Could not delete the value for key '" + location.key + "' under keychain '" +
                                    location.service + "'. Caused by: ";

        return [location = std::move(location), failure](SecureStore& store) {
            return describingFailure(failure, [&] {
                store.backend().remove(location);

                return true;
            });
        };
    }

    /** `(value, key, options)`, as upstream's JavaScript passes a set. */
    static std::function<bool(SecureStore&)> writing(Runtime& runtime, const Value* arguments, size_t count) {
        std::string value = argument(arguments, count, 0).asString(runtime).utf8(runtime);
        SecretLocation location = locationOf(runtime, arguments, count, 1);
        const std::optional<facebook::jsi::Object> options = optionsOf(runtime, arguments, count, 2);
        const std::string failure = "An error occurred when writing to key: '" + location.key + "' under keychain: '" +
                                    location.service + "'. Caused by: ";

        if (options.has_value() &&
            Value::strictEquals(runtime, options->getProperty(runtime, "requireAuthentication"), Value(true))) {
            throw std::invalid_argument(failure + "requireAuthentication is not supported on Linux");
        }

        return [value = std::move(value), location = std::move(location), failure](SecureStore& store) {
            return describingFailure(failure, [&] {
                store.backend().write(location, value);

                return true;
            });
        };
    }

    template <typename Result> Value schedule(Runtime& runtime, std::function<Result(SecureStore&)> work) {
        facebook::react::AsyncPromise<Result> promise(runtime, jsInvoker_);
        Value promiseObject = promise.get(runtime);

        store_.post([promise = std::move(promise), work = std::move(work)](SecureStore& store) mutable {
            try {
                promise.resolve(work(store));
            } catch (const std::exception& error) {
                promise.reject(facebook::react::Error(error.what()));
            }
        });

        return promiseObject;
    }

    template <typename Result> Result await(std::function<Result(SecureStore&)> work) {
        std::promise<Result> result;
        std::future<Result> settled = result.get_future();

        store_.post([&result, &work](SecureStore& store) {
            try {
                result.set_value(work(store));
            } catch (const std::exception&) {
                result.set_exception(std::current_exception());
            }
        });

        return settled.get();
    }

    SecureStore& store_;
};

} // namespace

std::shared_ptr<TurboModule> makeExpoSecureStoreModule(std::shared_ptr<facebook::react::CallInvoker> jsInvoker,
                                                       SecureStore& store) {
    return std::make_shared<LinuxSecureStoreModule>(std::move(jsInvoker), store);
}

} // namespace react_native_linux
