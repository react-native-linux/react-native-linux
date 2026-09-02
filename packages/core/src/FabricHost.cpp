#include "FabricHost.h"

#include "TextInputComponent.h"

#ifdef RNL_ENABLE_IMAGES
#include "ImageDecoder.h"
#endif

#include <jsi/jsi.h>
#include <react/renderer/componentregistry/ComponentDescriptorProviderRegistry.h>
#include <react/renderer/components/image/ImageComponentDescriptor.h>
#include <react/renderer/components/root/RootComponentDescriptor.h>
#include <react/renderer/components/scrollview/ScrollViewComponentDescriptor.h>
#include <react/renderer/components/text/ParagraphComponentDescriptor.h>
#include <react/renderer/components/text/RawTextComponentDescriptor.h>
#include <react/renderer/components/text/TextComponentDescriptor.h>
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
#include <vector>

namespace react_native_linux {

namespace {

constexpr facebook::react::SurfaceId kSurfaceId = 1;

// ComponentDescriptorRegistry keeps a reference to the provider registry that created it, so the provider
// registry has to outlive the Scheduler rather than the factory call.
//
// The set is StubComponentRegistryFactory's, minus the components that have no renderer behind them yet.
// `Text` and `RawText` are registered even though neither ever reaches the mounting layer — they are not
// view-forming, and `ParagraphShadowNode` flattens them into the `AttributedString` its state carries — because
// the reconciler cannot create a node whose component has no descriptor. `Paragraph`'s descriptor is what
// constructs the `TextLayoutManager`, so registering it is also what wires SkParagraph into Yoga measurement.
//
// `Image`'s descriptor resolves its `ImageManager` through `getManagerByName`, which constructs one when the
// context container carries no `"ImageManager"` entry — and this host inserts none, because the implementation
// behind that class is already ours. See src/ImageManager.cpp.
//
// `ScrollView` needs nothing beyond its descriptor: upstream owns the shadow node, the state and the event
// emitter, and the platform's whole contribution is moving `contentOffset`. See src/ScrollController.cpp.
//
// `TextInput` is the one component whose descriptor is ours rather than upstream's. Upstream ships the shared
// half — `BaseTextInputProps`, `BaseTextInputShadowNode`, `TextInputState` and `TextInputEventEmitter` — with no
// `platform/cxx` directory beside them, and its own CMakeLists globs `platform/android` unconditionally, so
// there is nothing to swap a source into. `src/TextInputComponent.h` therefore declares the descriptor, the
// shadow node and the props on top of those base classes; see *TextInput* in docs/cpp-toolchain.md.
facebook::react::ComponentRegistryFactory createComponentRegistryFactory(
    const std::shared_ptr<facebook::react::ComponentDescriptorProviderRegistry>& providerRegistry) {
    providerRegistry->add(
        facebook::react::concreteComponentDescriptorProvider<facebook::react::RootComponentDescriptor>());
    providerRegistry->add(
        facebook::react::concreteComponentDescriptorProvider<facebook::react::ViewComponentDescriptor>());
    providerRegistry->add(
        facebook::react::concreteComponentDescriptorProvider<facebook::react::ImageComponentDescriptor>());
    providerRegistry->add(
        facebook::react::concreteComponentDescriptorProvider<facebook::react::ScrollViewComponentDescriptor>());
    providerRegistry->add(
        facebook::react::concreteComponentDescriptorProvider<facebook::react::ParagraphComponentDescriptor>());
    providerRegistry->add(
        facebook::react::concreteComponentDescriptorProvider<facebook::react::TextComponentDescriptor>());
    providerRegistry->add(
        facebook::react::concreteComponentDescriptorProvider<facebook::react::RawTextComponentDescriptor>());
    providerRegistry->add(facebook::react::concreteComponentDescriptorProvider<TextInputComponentDescriptor>());

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

/**
 * The platform `EventBeat`, which upstream requires every host to subclass because only the host knows when a
 * frame's events are complete.
 *
 * `EventBeat::induce` is protected and does nothing until an `EventQueue` has asked for a beat, so a subclass that
 * widens it is the entire platform contribution: the queue requests, the frame thread induces, and the base class
 * hands the flush to `RuntimeScheduler::scheduleWork`, which is what puts the beat callback on the JavaScript
 * thread. `ReactCxxPlatform`'s `RunLoopObserverManager` produces the same behaviour through a `RunLoopObserver`
 * whose `startObserving` and `stopObserving` are empty; there is no run loop to observe here either, and its
 * `induce` is declared but never defined, so the observer indirection would buy an unresolved symbol.
 */
class FrameEventBeat final : public facebook::react::EventBeat {
public:
    using facebook::react::EventBeat::EventBeat;

    void induceFromFrameThread() const { induce(); }
};

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
    // Scheduler calls this factory exactly once, synchronously, from the constructor below, and does not retain the
    // toolbox; the beat it produces lives inside the EventDispatcher for as long as the Scheduler does.
    schedulerToolbox.eventBeatFactory =
        [runtimeScheduler, this](std::shared_ptr<facebook::react::EventBeat::OwnerBox> ownerBox) {
            std::unique_ptr<FrameEventBeat> eventBeat =
                std::make_unique<FrameEventBeat>(std::move(ownerBox), *runtimeScheduler);

            eventBeatInducer_ = [beat = eventBeat.get()]() { beat->induceFromFrameThread(); };

            return eventBeat;
        };

    schedulerDelegate_ = std::make_unique<facebook::react::SchedulerDelegateImpl>(mountingManager_);
    scheduler_ = std::make_unique<facebook::react::Scheduler>(schedulerToolbox, nullptr, schedulerDelegate_.get());
    schedulerDelegate_->setUIManager(scheduler_->getUIManager());
    inputDispatcher_ = std::make_unique<InputDispatcher>(scheduler_->getUIManager(), mountingManager_, kSurfaceId);
    scrollController_ = std::make_unique<ScrollController>(scheduler_->getUIManager(), kSurfaceId);

    reactInstance.getUnbufferedRuntimeExecutor()(installStopSurfaceBinding);

#ifdef RNL_ENABLE_IMAGES
    // A finished decode changes the picture with no Fabric mutation behind it, so this is the only path that can
    // damage the frame for one. The listener runs on the decode thread and takes the mounting manager's mutex; the
    // pipeline holds no lock of its own while it calls back.
    setImageDecodeListener([mountingManager = mountingManager_](const std::string& uri) {
        mountingManager->damageImageSource(uri);
    });
#endif

    mountingManager_->startSurface(kSurfaceId, surfaceSize);

    surfaceHandler_ = std::make_unique<facebook::react::SurfaceHandler>("", kSurfaceId);
    scheduler_->registerSurface(*surfaceHandler_);
    setSurfaceSize(surfaceSize);
    surfaceHandler_->start();
}

FabricHost::~FabricHost() noexcept {
#ifdef RNL_ENABLE_IMAGES
    setImageDecodeListener({});
#endif
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

void FabricHost::setTextInputFocusSink(TextInputFocusSink* textInputFocusSink) {
    inputDispatcher_->setTextInputFocusSink(textInputFocusSink);
}

void FabricHost::dispatchInput(const std::vector<InputEvent>& events) {
    std::vector<InputEvent> pointerEvents;

    // Scroll and pointer routing answer different questions about the same coordinate — which ScrollView owns it
    // versus which node was clicked — so the frame is split rather than hit-tested twice for events one of them
    // would discard.
    for (const InputEvent& event : events) {
        if (!isScrollEvent(event)) {
            pointerEvents.push_back(event);
        }
    }

    scrollController_->dispatch(events);
    inputDispatcher_->dispatch(pointerEvents);
}

bool FabricHost::advanceScroll(double frameMilliseconds) { return scrollController_->advance(frameMilliseconds); }

bool FabricHost::advanceCaretBlink(double frameMilliseconds) {
    return inputDispatcher_->advanceCaretBlink(frameMilliseconds);
}

void FabricHost::induceEventBeat() { eventBeatInducer_(); }

SceneFrame FabricHost::takeFrame() {
    return mountingManager_->takeFrame();
}

SceneSnapshot FabricHost::snapshotScene() const {
    return mountingManager_->snapshotScene();
}

std::string FabricHost::dumpScene() const {
    return mountingManager_->dumpScene();
}

} // namespace react_native_linux
