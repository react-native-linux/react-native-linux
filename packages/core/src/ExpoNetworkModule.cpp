#include "ExpoNetworkModule.h"

#include <ReactCommon/CallInvoker.h>
#include <ReactCommon/TurboModule.h>
#include <ReactCommon/TurboModuleUtils.h>
#include <cstddef>
#include <jsi/jsi.h>
#include <memory>
#include <utility>

#include <react/bridging/EventEmitter.h>

namespace react_native_linux {
namespace {

class ExpoNetworkModule final : public facebook::react::TurboModule {
public:
    explicit ExpoNetworkModule(std::shared_ptr<facebook::react::CallInvoker> jsInvoker)
        : TurboModule("ExpoNetwork", std::move(jsInvoker)) {
        methodMap_["getNetworkStateAsync"] = {0, &getNetworkStateAsync};
        eventEmitterMap_["onNetworkStateChanged"] = std::make_shared<facebook::react::AsyncEventEmitter<>>();
    }

private:
    static facebook::jsi::Value getNetworkStateAsync(facebook::jsi::Runtime& runtime, TurboModule& /*turboModule*/,
                                                     const facebook::jsi::Value* /*arguments*/, size_t /*count*/) {
        return facebook::react::createPromiseAsJSIValue(
            runtime,
            [](facebook::jsi::Runtime& promiseRuntime, const std::shared_ptr<facebook::react::Promise>& promise) {
                facebook::jsi::Object unknownState(promiseRuntime);

                unknownState.setProperty(promiseRuntime, "type", "UNKNOWN");
                promise->resolve(facebook::jsi::Value(promiseRuntime, unknownState));
            });
    }
};

} // namespace

std::shared_ptr<facebook::react::TurboModule>
makeExpoNetworkModule(std::shared_ptr<facebook::react::CallInvoker> jsInvoker) {
    return std::make_shared<ExpoNetworkModule>(std::move(jsInvoker));
}

} // namespace react_native_linux
