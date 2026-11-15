#include "T3NativeControls.h"

#include "DpopKeys.h"

#include <ReactCommon/TurboModuleUtils.h>
#include <algorithm>
#include <stdexcept>
#include <string>
#include <utility>

namespace facebook::react {
namespace {

using t3_native_controls::Bytes;
using t3_native_controls::DpopKeys;

/** The bytes of a `Uint8Array`, or of any other view over an `ArrayBuffer`. */
Bytes bytesOf(jsi::Runtime& runtime, const jsi::Value* arguments, std::size_t count, std::size_t index) {
    if (index >= count || !arguments[index].isObject()) {
        throw std::invalid_argument("T3NativeControls: argument " + std::to_string(index) + " is not a Uint8Array");
    }

    const jsi::Object view = arguments[index].getObject(runtime);
    const jsi::ArrayBuffer buffer = view.getPropertyAsObject(runtime, "buffer").getArrayBuffer(runtime);
    const double offset = view.getProperty(runtime, "byteOffset").asNumber();
    const double length = view.getProperty(runtime, "byteLength").asNumber();

    if (!(offset >= 0 && length >= 0 && offset + length <= static_cast<double>(buffer.size(runtime)))) {
        throw std::invalid_argument("T3NativeControls: argument " + std::to_string(index) + " is not a Uint8Array");
    }

    const std::uint8_t* begin = buffer.data(runtime) + static_cast<std::size_t>(offset);

    return {begin, begin + static_cast<std::size_t>(length)};
}

jsi::Value uint8ArrayOf(jsi::Runtime& runtime, const Bytes& bytes) {
    jsi::Object array = runtime.global()
                            .getPropertyAsFunction(runtime, "Uint8Array")
                            .callAsConstructor(runtime, static_cast<double>(bytes.size()))
                            .getObject(runtime);

    std::ranges::copy(bytes, array.getPropertyAsObject(runtime, "buffer").getArrayBuffer(runtime).data(runtime));

    return array;
}

/** Expo's `AsyncFunction` contract: a thrown error rejects the promise with its message. */
template <typename Operation> jsi::Value settle(jsi::Runtime& runtime, Operation&& operation) {
    return createPromiseAsJSIValue(runtime, [&](jsi::Runtime& promiseRuntime, const std::shared_ptr<Promise>& promise) {
        try {
            promise->resolve(operation(promiseRuntime));
        } catch (const std::exception& error) {
            promise->reject(error.what());
        }
    });
}

} // namespace

T3NativeControls::T3NativeControls(std::shared_ptr<CallInvoker> jsInvoker)
    : TurboModule(std::string(kModuleName), std::move(jsInvoker)) {
    methodMap_["generateDpopKey"] = {
        0, [](jsi::Runtime& runtime, TurboModule& /*turboModule*/, const jsi::Value* /*arguments*/, std::size_t) {
            return settle(runtime, [](jsi::Runtime& promiseRuntime) {
                return uint8ArrayOf(promiseRuntime, DpopKeys::generate());
            });
        }};
    methodMap_["dpopKeyMatches"] = {
        2, [](jsi::Runtime& runtime, TurboModule& /*turboModule*/, const jsi::Value* arguments, std::size_t count) {
            return settle(runtime, [&](jsi::Runtime& promiseRuntime) {
                return jsi::Value(DpopKeys::matches(bytesOf(promiseRuntime, arguments, count, 0),
                                                    bytesOf(promiseRuntime, arguments, count, 1)));
            });
        }};
    methodMap_["signDpop"] = {
        2, [](jsi::Runtime& runtime, TurboModule& /*turboModule*/, const jsi::Value* arguments, std::size_t count) {
            return settle(runtime, [&](jsi::Runtime& promiseRuntime) {
                return uint8ArrayOf(promiseRuntime, DpopKeys::sign(bytesOf(promiseRuntime, arguments, count, 0),
                                                                   bytesOf(promiseRuntime, arguments, count, 1)));
            });
        }};
}

} // namespace facebook::react
