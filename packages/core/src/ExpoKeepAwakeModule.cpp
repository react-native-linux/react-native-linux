#include "ExpoKeepAwakeModule.h"

#include <ReactCommon/CallInvoker.h>
#include <ReactCommon/TurboModule.h>
#include <ReactCommon/TurboModuleUtils.h>
#include <cstddef>
#include <jsi/jsi.h>
#include <memory>
#include <string>
#include <utility>

namespace react_native_linux {
namespace {

/** Upstream's `UnavailabilityError`: a `CodedError` whose code is `ERR_UNAVAILABLE`. */
facebook::jsi::Value rejectUnavailable(facebook::jsi::Runtime& runtime, const std::string& method) {
    return facebook::react::createPromiseAsJSIValue(
        runtime,
        [method](facebook::jsi::Runtime& promiseRuntime, const std::shared_ptr<facebook::react::Promise>& promise) {
            const std::string message = "The method or property ExpoKeepAwake." + method + " is not available on linux";
            facebook::jsi::Object error =
                promiseRuntime.global()
                    .getPropertyAsFunction(promiseRuntime, "Error")
                    .callAsConstructor(promiseRuntime, facebook::jsi::String::createFromUtf8(promiseRuntime, message))
                    .asObject(promiseRuntime);

            error.setProperty(promiseRuntime, "code", "ERR_UNAVAILABLE");
            promise->reject_.call(promiseRuntime, error);
        });
}

class ExpoKeepAwakeModule final : public facebook::react::TurboModule {
public:
    explicit ExpoKeepAwakeModule(std::shared_ptr<facebook::react::CallInvoker> jsInvoker)
        : TurboModule("ExpoKeepAwake", std::move(jsInvoker)) {
        methodMap_["isAvailableAsync"] = {
            0, [](facebook::jsi::Runtime& runtime, TurboModule& /*turboModule*/,
                  const facebook::jsi::Value* /*arguments*/, size_t /*count*/) {
                const facebook::jsi::Object promise = runtime.global().getPropertyAsObject(runtime, "Promise");

                return promise.getPropertyAsFunction(runtime, "resolve").callWithThis(runtime, promise, false);
            }};
        methodMap_["activate"] = {1, [](facebook::jsi::Runtime& runtime, TurboModule& /*turboModule*/,
                                        const facebook::jsi::Value* /*arguments*/,
                                        size_t /*count*/) { return rejectUnavailable(runtime, "activate"); }};
        methodMap_["deactivate"] = {1, [](facebook::jsi::Runtime& runtime, TurboModule& /*turboModule*/,
                                          const facebook::jsi::Value* /*arguments*/,
                                          size_t /*count*/) { return rejectUnavailable(runtime, "deactivate"); }};
    }
};

} // namespace

std::shared_ptr<facebook::react::TurboModule>
makeExpoKeepAwakeModule(std::shared_ptr<facebook::react::CallInvoker> jsInvoker) {
    return std::make_shared<ExpoKeepAwakeModule>(std::move(jsInvoker));
}

} // namespace react_native_linux
