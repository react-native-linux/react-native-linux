#include "ExpoCryptoModule.h"

#include "ExpoCrypto.h"
#include "ExpoFileSystem.h"
#include "ExpoSharedObject.h"

#include <ReactCommon/CallInvoker.h>
#include <ReactCommon/TurboModule.h>
#include <ReactCommon/TurboModuleUtils.h>
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <jsi/jsi.h>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace react_native_linux {
namespace {

using facebook::jsi::Runtime;
using facebook::jsi::Value;
using facebook::react::TurboModule;

const Value& argument(const Value* arguments, size_t count, size_t index) {
    if (index >= count) {
        throw std::invalid_argument("ExpoCrypto: missing argument " + std::to_string(index));
    }

    return arguments[index];
}

/** The bytes a typed array covers inside its `ArrayBuffer`, writable in place. */
std::span<std::uint8_t> viewBytes(Runtime& runtime, const Value& view) {
    const facebook::jsi::Object object = view.asObject(runtime);
    facebook::jsi::ArrayBuffer buffer = object.getPropertyAsObject(runtime, "buffer").getArrayBuffer(runtime);
    const auto [offset, length] =
        byteViewRange(object.getProperty(runtime, "byteOffset").asNumber(),
                      object.getProperty(runtime, "byteLength").asNumber(), buffer.size(runtime));

    return {buffer.data(runtime) + offset, length};
}

Value digest(Runtime& runtime, TurboModule& /*module*/, const Value* arguments, size_t count) {
    const std::string algorithm = argument(arguments, count, 0).asString(runtime).utf8(runtime);
    const std::span<std::uint8_t> output = viewBytes(runtime, argument(arguments, count, 1));
    const std::vector<std::uint8_t> digest = cryptoDigest(algorithm, viewBytes(runtime, argument(arguments, count, 2)));

    std::copy_n(digest.begin(), std::min(digest.size(), output.size()), output.begin());

    return Value::undefined();
}

Value digestStringAsync(Runtime& runtime, TurboModule& /*module*/, const Value* arguments, size_t count) {
    return facebook::react::createPromiseAsJSIValue(
        runtime, [&](Runtime& promiseRuntime, const std::shared_ptr<facebook::react::Promise>& promise) {
            try {
                const Value encoding =
                    count > 2 && arguments[2].isObject()
                        ? arguments[2].asObject(promiseRuntime).getProperty(promiseRuntime, "encoding")
                        : Value::undefined();

                promise->resolve(facebook::jsi::String::createFromUtf8(
                    promiseRuntime,
                    encodedCryptoDigest(
                        argument(arguments, count, 0).asString(promiseRuntime).utf8(promiseRuntime),
                        argument(arguments, count, 1).asString(promiseRuntime).utf8(promiseRuntime),
                        encoding.isUndefined() ? "hex" : encoding.asString(promiseRuntime).utf8(promiseRuntime))));
            } catch (const std::exception& error) {
                promise->reject(error.what());
            }
        });
}

Value getRandomValues(Runtime& runtime, TurboModule& /*module*/, const Value* arguments, size_t count) {
    const Value& array = argument(arguments, count, 0);

    fillWithRandomBytes(viewBytes(runtime, array));

    return {runtime, array};
}

Value randomUUID(Runtime& runtime, TurboModule& /*module*/, const Value* /*arguments*/, size_t /*count*/) {
    return facebook::jsi::String::createFromAscii(runtime, randomUuid());
}

class ExpoCryptoModule final : public TurboModule {
public:
    explicit ExpoCryptoModule(std::shared_ptr<facebook::react::CallInvoker> jsInvoker)
        : TurboModule("ExpoCrypto", std::move(jsInvoker)) {
        methodMap_["digest"] = {3, &digest};
        methodMap_["digestStringAsync"] = {3, &digestStringAsync};
        methodMap_["getRandomValues"] = {1, &getRandomValues};
        methodMap_["randomUUID"] = {0, &randomUUID};
    }
};

std::shared_ptr<facebook::jsi::NativeState> unavailableAes(Runtime& /*runtime*/, const Value* /*arguments*/,
                                                           size_t /*count*/) {
    throw std::invalid_argument("expo-crypto's AES is not available on Linux");
}

class ExpoCryptoAESModule final : public TurboModule {
public:
    explicit ExpoCryptoAESModule(std::shared_ptr<facebook::react::CallInvoker> jsInvoker)
        : TurboModule("ExpoCryptoAES", std::move(jsInvoker)) {
        methodMap_["getConstants"] = {
            0, [](Runtime& runtime, TurboModule&, const Value*, size_t) -> Value {
                facebook::jsi::Object constants(runtime);

                for (const char* name : {"EncryptionKey", "SealedData"}) {
                    constants.setProperty(runtime, name, makeSharedObjectClass(runtime, &unavailableAes, {}, {}));
                }

                return constants;
            }};
    }
};

} // namespace

std::shared_ptr<TurboModule> makeExpoCryptoModule(std::shared_ptr<facebook::react::CallInvoker> jsInvoker) {
    return std::make_shared<ExpoCryptoModule>(std::move(jsInvoker));
}

std::shared_ptr<TurboModule> makeExpoCryptoAESModule(std::shared_ptr<facebook::react::CallInvoker> jsInvoker) {
    return std::make_shared<ExpoCryptoAESModule>(std::move(jsInvoker));
}

} // namespace react_native_linux
