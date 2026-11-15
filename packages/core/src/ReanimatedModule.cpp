#include "ReanimatedModule.h"

#include "ReanimatedPlatform.h"

#include <memory>
#include <reanimated/NativeModules/ReanimatedModuleProxy.h>
#include <reanimated/RuntimeDecorators/RNRuntimeDecorator.h>
#include <reanimated/Tools/FeatureFlags.h>
#include <string>
#include <utility>

#include <react/renderer/scheduler/Scheduler.h>
#include <react/renderer/uimanager/IMountingManager.h>
#include <react/renderer/uimanager/UIManagerBinding.h>

namespace react_native_linux {

// `StaticFeatureFlags::getFlag` answers false for a flag the REANIMATED_FEATURE_FLAGS string lacks, which would
// silently select the legacy loop that needs a platform `performOperations` this host does not have.
static_assert(reanimated::StaticFeatureFlags::getFlag("USE_ANIMATION_BACKEND"),
              "packages/core/CMakeLists.txt compiles Reanimated with USE_ANIMATION_BACKEND");

LinuxReanimatedModule::LinuxReanimatedModule(std::shared_ptr<facebook::react::CallInvoker> jsInvoker,
                                             std::shared_ptr<LinuxWorkletsModule> worklets)
    : TurboModule(std::string(kModuleName), std::move(jsInvoker)), worklets_(std::move(worklets)) {
    methodMap_["installTurboModule"] = MethodMetadata{
        .argCount = 0,
        .invoker = [](facebook::jsi::Runtime& runtime, TurboModule& module, const facebook::jsi::Value* /*arguments*/,
                      size_t /*count*/) -> facebook::jsi::Value {
            return static_cast<LinuxReanimatedModule&>(module).installProxy(runtime);
        }};
}

bool LinuxReanimatedModule::installProxy(facebook::jsi::Runtime& runtime) {
    const std::shared_ptr<facebook::react::UIManagerBinding> binding =
        facebook::react::UIManagerBinding::getBinding(runtime);

    if (binding == nullptr) {
        return false;
    }

    facebook::react::UIManager& uiManager = binding->getUIManager();
    const std::shared_ptr<worklets::WorkletRuntime> uiRuntime = worklets_->uiWorkletRuntime();
    const reanimated::PlatformDepMethodsHolder platform = makePlatformDepMethodsHolder(
        worklets_->animationFrames(),
        uiManager.getContextContainer()->at<std::shared_ptr<facebook::react::IMountingManager>>(
            kMountingManagerContextKey));

    proxy_ = std::make_shared<reanimated::ReanimatedModuleProxy>(uiRuntime, worklets_->uiScheduler(), runtime,
                                                                 jsInvoker_, platform, false);
    // Fabric first and the UI runtime's bindings last, so no worklet can reach the proxy before it has a UIManager.
    proxy_->initializeFabric(dynamic_cast<facebook::react::Scheduler&>(*uiManager.getDelegate()).getUIManager());
    proxy_->init(platform);
    reanimated::RNRuntimeDecorator::decorate(runtime, worklets::getJSIRuntimeFromWorkletRuntime(uiRuntime), proxy_);

    return true;
}

void LinuxReanimatedModule::invalidate() { proxy_.reset(); }

} // namespace react_native_linux
