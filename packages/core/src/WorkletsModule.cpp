#include "WorkletsModule.h"

#include <functional>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <worklets/NativeModules/WorkletsModuleProxy.h>
#include <worklets/WorkletRuntime/BundleModeConfig.h>
#include <worklets/WorkletRuntime/RuntimeBindings.h>

namespace react_native_linux {

LinuxWorkletsModule::LinuxWorkletsModule(std::shared_ptr<facebook::react::CallInvoker> jsInvoker)
    : TurboModule(std::string(kModuleName), std::move(jsInvoker)) {
    methodMap_["installTurboModule"] =
        MethodMetadata{.argCount = 1,
                       .invoker = [](facebook::jsi::Runtime& runtime, TurboModule& module,
                                     const facebook::jsi::Value* arguments, size_t /*count*/) -> facebook::jsi::Value {
                           static_cast<LinuxWorkletsModule&>(module).installProxy(runtime, arguments[0].getBool());
                           return true;
                       }};
    methodMap_["start"] = MethodMetadata{.argCount = 0,
                                         .invoker = [](facebook::jsi::Runtime& /*runtime*/, TurboModule& module,
                                                       const facebook::jsi::Value* /*arguments*/,
                                                       size_t /*count*/) -> facebook::jsi::Value {
                                             static_cast<LinuxWorkletsModule&>(module).start();
                                             return true;
                                         }};
    methodMap_["toggleSlowAnimationsOnUIRuntime"] = MethodMetadata{
        .argCount = 0,
        .invoker = [](facebook::jsi::Runtime& /*runtime*/, TurboModule& /*module*/,
                      const facebook::jsi::Value* /*arguments*/, size_t /*count*/) -> facebook::jsi::Value {
            throw std::runtime_error("[Worklets] toggleSlowAnimationsOnUIRuntime is not supported on Linux.");
        }};
}

void LinuxWorkletsModule::installProxy(facebook::jsi::Runtime& runtime, bool bundleModeEnabled) {
    if (bundleModeEnabled) {
        throw std::runtime_error("[Worklets] Bundle Mode is not supported on Linux yet.");
    }

    auto runtimeBindings = std::make_shared<worklets::RuntimeBindings>(worklets::RuntimeBindings{
        .requestAnimationFrame =
            [this](std::function<void(const double)>&& callback) { animationFrames_.request(std::move(callback)); },
        .nativeLoggingHook = {}});
    proxy_ = std::make_shared<worklets::WorkletsModuleProxy>(
        runtime, jsInvoker_, uiScheduler_,
        [javaScriptThread = std::this_thread::get_id()]() { return std::this_thread::get_id() == javaScriptThread; },
        runtimeBindings, worklets::BundleModeConfig{}, rnRuntimeStatus_);
}

void LinuxWorkletsModule::start() { proxy_->start(); }

void LinuxWorkletsModule::tick(std::chrono::steady_clock::time_point now) {
    uiScheduler_->triggerUI();
    animationFrames_.dispatchFrame(std::chrono::duration<double, std::milli>(now.time_since_epoch()).count());
}

bool LinuxWorkletsModule::hasPendingWork() const {
    return uiScheduler_->hasPendingJobs() || animationFrames_.hasPendingRequests();
}

std::shared_ptr<worklets::WorkletRuntime> LinuxWorkletsModule::uiWorkletRuntime() const {
    return proxy_ == nullptr ? nullptr : proxy_->getUIWorkletRuntime();
}

void LinuxWorkletsModule::invalidate() {
    rnRuntimeStatus_->setDead();
    proxy_.reset();
    animationFrames_.clear();
}

} // namespace react_native_linux
