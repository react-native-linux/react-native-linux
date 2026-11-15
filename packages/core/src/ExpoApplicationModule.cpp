#include "ExpoApplicationModule.h"

#include <ReactCommon/CallInvoker.h>
#include <ReactCommon/TurboModule.h>
#include <cstddef>
#include <initializer_list>
#include <jsi/jsi.h>
#include <memory>
#include <utility>

namespace react_native_linux {
namespace {

class ExpoApplicationModule final : public facebook::react::TurboModule {
public:
    explicit ExpoApplicationModule(std::shared_ptr<facebook::react::CallInvoker> jsInvoker)
        : TurboModule("ExpoApplication", std::move(jsInvoker)) {
        methodMap_["getConstants"] = {0, &identity};
    }

private:
    static facebook::jsi::Value identity(facebook::jsi::Runtime& runtime, facebook::react::TurboModule& /*module*/,
                                         const facebook::jsi::Value* /*arguments*/, std::size_t /*count*/) {
        facebook::jsi::Object identity(runtime);

        for (const char* name :
             {"applicationId", "applicationName", "nativeApplicationVersion", "nativeBuildVersion"}) {
            identity.setProperty(runtime, name, facebook::jsi::Value::null());
        }

        return identity;
    }
};

} // namespace

std::shared_ptr<facebook::react::TurboModule>
makeExpoApplicationModule(std::shared_ptr<facebook::react::CallInvoker> jsInvoker) {
    return std::make_shared<ExpoApplicationModule>(std::move(jsInvoker));
}

} // namespace react_native_linux
