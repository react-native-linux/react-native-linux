#include "ExpoLinkingModule.h"

#include "Activation.h"

#include <ReactCommon/TurboModule.h>
#include <cstddef>
#include <jsi/jsi.h>
#include <memory>
#include <optional>
#include <string>
#include <utility>

namespace react_native_linux {
namespace {

class ExpoLinkingModule final : public facebook::react::TurboModule {
public:
    ExpoLinkingModule(std::shared_ptr<facebook::react::CallInvoker> jsInvoker,
                      std::shared_ptr<ActivationModel> activationModel)
        : TurboModule("ExpoLinking", std::move(jsInvoker)), activationModel_(std::move(activationModel)) {
        methodMap_["getLinkingURL"] = {0, &getLinkingURL};
        methodMap_["onURLReceived"] = {1, &onURLReceived};
    }

private:
    static facebook::jsi::Value getLinkingURL(facebook::jsi::Runtime& runtime, TurboModule& turboModule,
                                              const facebook::jsi::Value* /*arguments*/, size_t /*count*/) {
        const std::optional<std::string> url =
            static_cast<ExpoLinkingModule&>(turboModule).activationModel_->currentUrl();

        if (!url.has_value()) {
            return facebook::jsi::Value::null();
        }

        return facebook::jsi::String::createFromUtf8(runtime, url.value());
    }

    static facebook::jsi::Value onURLReceived(facebook::jsi::Runtime& runtime, TurboModule& /*turboModule*/,
                                              const facebook::jsi::Value* arguments, size_t count) {
        if (count < 1) {
            throw facebook::jsi::JSError(runtime, "ExpoLinking.onURLReceived takes a listener");
        }

        const facebook::jsi::Object deviceEventEmitter =
            runtime.global().getPropertyAsObject(runtime, "__rctDeviceEventEmitter");

        return deviceEventEmitter.getPropertyAsFunction(runtime, "addListener")
            .callWithThis(runtime, deviceEventEmitter, facebook::jsi::String::createFromAscii(runtime, "url"),
                          arguments[0]);
    }

    std::shared_ptr<ActivationModel> activationModel_;
};

} // namespace

std::shared_ptr<facebook::react::TurboModule>
makeExpoLinkingModule(std::shared_ptr<facebook::react::CallInvoker> jsInvoker,
                      std::shared_ptr<ActivationModel> activationModel) {
    return std::make_shared<ExpoLinkingModule>(std::move(jsInvoker), std::move(activationModel));
}

} // namespace react_native_linux
