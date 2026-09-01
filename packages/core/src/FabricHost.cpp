#include "FabricHost.h"

#include <jsi/jsi.h>
#include <react/renderer/componentregistry/ComponentDescriptorProviderRegistry.h>
#include <react/renderer/components/root/RootComponentDescriptor.h>
#include <react/renderer/components/view/ViewComponentDescriptor.h>
#include <react/renderer/core/EventBeat.h>
#include <react/renderer/core/LayoutConstraints.h>
#include <react/renderer/core/LayoutPrimitives.h>
#include <react/renderer/runtimescheduler/RuntimeScheduler.h>
#include <react/renderer/scheduler/SchedulerToolbox.h>

#include <cstddef>
#include <memory>
#include <string>
#include <utility>

namespace react_native_linux {

namespace {

constexpr facebook::react::SurfaceId kSurfaceId = 1;

// ComponentDescriptorRegistry keeps a reference to the provider registry that created it, so the provider
// registry has to outlive the Scheduler rather than the factory call.
facebook::react::ComponentRegistryFactory createComponentRegistryFactory(
    const std::shared_ptr<facebook::react::ComponentDescriptorProviderRegistry>& providerRegistry) {
    providerRegistry->add(
        facebook::react::concreteComponentDescriptorProvider<facebook::react::RootComponentDescriptor>());
    providerRegistry->add(
        facebook::react::concreteComponentDescriptorProvider<facebook::react::ViewComponentDescriptor>());

    return [providerRegistry](const facebook::react::EventDispatcher::Weak& eventDispatcher,
                              const std::shared_ptr<const facebook::react::ContextContainer>& contextContainer) {
        return providerRegistry->createComponentDescriptorRegistry(facebook::react::ComponentDescriptorParameters{
            .eventDispatcher = eventDispatcher, .contextContainer = contextContainer, .flavor = nullptr});
    };
}

// AppRegistryBinding::stopSurface throws when RN$stopSurface is absent, and SurfaceHandler::stop goes through it
// even for a surface that was started empty. React Native's JavaScript surface registry is not part of this host.
void installStopSurfaceBinding(facebook::jsi::Runtime& runtime) {
    facebook::jsi::Function stopSurface = facebook::jsi::Function::createFromHostFunction(
        runtime, facebook::jsi::PropNameID::forAscii(runtime, "RN$stopSurface"), 1,
        [](facebook::jsi::Runtime& /*hostRuntime*/, const facebook::jsi::Value& /*thisValue*/,
           const facebook::jsi::Value* /*arguments*/, size_t /*argumentCount*/) {
            return facebook::jsi::Value::undefined();
        });

    runtime.global().setProperty(runtime, "RN$stopSurface", stopSurface);
}

} // namespace

FabricHost::FabricHost(facebook::react::ReactInstance& reactInstance, facebook::react::Size surfaceSize)
    : contextContainer_(std::make_shared<const facebook::react::ContextContainer>()),
      componentDescriptorProviderRegistry_(
          std::make_shared<facebook::react::ComponentDescriptorProviderRegistry>()),
      mountingManager_(std::make_shared<LinuxMountingManager>()) {
    const std::shared_ptr<facebook::react::RuntimeScheduler> runtimeScheduler = reactInstance.getRuntimeScheduler();
    contextContainer_->insert(facebook::react::RuntimeSchedulerKey,
                              std::weak_ptr<facebook::react::RuntimeScheduler>(runtimeScheduler));

    facebook::react::SchedulerToolbox schedulerToolbox;
    schedulerToolbox.contextContainer = contextContainer_;
    schedulerToolbox.runtimeExecutor = reactInstance.getBufferedRuntimeExecutor();
    schedulerToolbox.bridgelessBindingsExecutor = reactInstance.getUnbufferedRuntimeExecutor();
    schedulerToolbox.componentRegistryFactory = createComponentRegistryFactory(componentDescriptorProviderRegistry_);
    schedulerToolbox.eventBeatFactory =
        [runtimeScheduler](std::shared_ptr<facebook::react::EventBeat::OwnerBox> ownerBox) {
            return std::make_unique<facebook::react::EventBeat>(std::move(ownerBox), *runtimeScheduler);
        };

    schedulerDelegate_ = std::make_unique<facebook::react::SchedulerDelegateImpl>(mountingManager_);
    scheduler_ = std::make_unique<facebook::react::Scheduler>(schedulerToolbox, nullptr, schedulerDelegate_.get());
    schedulerDelegate_->setUIManager(scheduler_->getUIManager());

    reactInstance.getUnbufferedRuntimeExecutor()(installStopSurfaceBinding);

    mountingManager_->startSurface(kSurfaceId, surfaceSize);

    surfaceHandler_ = std::make_unique<facebook::react::SurfaceHandler>("", kSurfaceId);
    scheduler_->registerSurface(*surfaceHandler_);
    setSurfaceSize(surfaceSize);
    surfaceHandler_->start();
}

FabricHost::~FabricHost() noexcept {
    stopSurface();
    scheduler_->unregisterSurface(*surfaceHandler_);
}

void FabricHost::setSurfaceSize(facebook::react::Size surfaceSize) {
    surfaceHandler_->constraintLayout({.minimumSize = surfaceSize,
                                       .maximumSize = surfaceSize,
                                       .layoutDirection = facebook::react::LayoutDirection::LeftToRight},
                                      {});
}

void FabricHost::stopSurface() {
    if (surfaceHandler_->getStatus() == facebook::react::SurfaceHandler::Status::Running) {
        surfaceHandler_->stop();
    }
}

SceneSnapshot FabricHost::snapshotScene() const {
    return mountingManager_->snapshotScene();
}

std::string FabricHost::dumpScene() const {
    return mountingManager_->dumpScene();
}

} // namespace react_native_linux
