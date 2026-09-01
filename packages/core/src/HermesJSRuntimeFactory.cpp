#include "HermesJSRuntimeFactory.h"

#include <hermes/hermes.h>
#include <jsi/jsi.h>

#include <memory>
#include <utility>

namespace react_native_linux {

std::unique_ptr<facebook::react::JSRuntime> HermesJSRuntimeFactory::createJSRuntime(
    std::shared_ptr<facebook::react::MessageQueueThread> /*messageQueueThread*/) noexcept {
    const ::hermes::vm::GCConfig gcConfig = ::hermes::vm::GCConfig::Builder().withName("ReactNativeLinux").build();
    const ::hermes::vm::RuntimeConfig runtimeConfig =
        ::hermes::vm::RuntimeConfig::Builder().withGCConfig(gcConfig).withMicrotaskQueue(true).build();

    std::unique_ptr<facebook::hermes::HermesRuntime> hermesRuntime = facebook::hermes::makeHermesRuntime(runtimeConfig);

    hermesRuntime->global()
        .getPropertyAsObject(*hermesRuntime, "Error")
        .getPropertyAsObject(*hermesRuntime, "prototype")
        .setProperty(*hermesRuntime, "jsEngine", "hermes");

    return std::make_unique<facebook::react::JSIRuntimeHolder>(std::move(hermesRuntime));
}

} // namespace react_native_linux
