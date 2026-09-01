#pragma once

#include <cxxreact/MessageQueueThread.h>
#include <react/runtime/JSRuntimeFactory.h>

#include <memory>

namespace react_native_linux {

class HermesJSRuntimeFactory final : public facebook::react::JSRuntimeFactory {
public:
    std::unique_ptr<facebook::react::JSRuntime>
    createJSRuntime(std::shared_ptr<facebook::react::MessageQueueThread> messageQueueThread) noexcept override;
};

} // namespace react_native_linux
