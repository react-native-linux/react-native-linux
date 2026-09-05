#pragma once

#include "InputDispatcher.h"
#include "InputPipeline.h"
#include "LinuxAnimationChoreographer.h"
#include "LinuxMountingManager.h"
#include "RetainedScene.h"
#include "ScrollController.h"

#include <react/renderer/componentregistry/ComponentDescriptorProviderRegistry.h>
#include <react/renderer/graphics/Size.h>
#include <react/renderer/scheduler/Scheduler.h>
#include <react/renderer/scheduler/SchedulerDelegateImpl.h>
#include <react/renderer/scheduler/SurfaceHandler.h>
#include <react/renderer/uimanager/UIManagerAnimationBackend.h>
#include <react/runtime/ReactInstance.h>
#include <react/utils/ContextContainer.h>

#include <chrono>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace react_native_linux {

/**
 * Bootstraps Fabric on top of an already initialised bridgeless `ReactInstance`: one `Scheduler`, one
 * `SurfaceHandler` started as an empty surface, and the retained scene the resulting mounting transactions are
 * applied to. The surface is sized by the caller: headlessly that is a fixed size, and under a window it is the
 * window's size, republished through `setSurfaceSize` on every configure so Yoga relayouts against it.
 *
 * The surface is started with an empty module name, which takes `SurfaceHandler` through
 * `UIManager::startEmptySurface` instead of `AppRegistryBinding`. That is what makes a JavaScript bundle without
 * React Native's own JavaScript runtime able to drive the renderer.
 *
 * Threading contract: construction and destruction happen on the thread that owns the process run loop, before
 * the JavaScript bundle is loaded and after the JavaScript thread has been drained. Everything Fabric does in
 * between happens on the JavaScript thread. `setSurfaceSize`, `takeFrame` and `snapshotScene` are the members that
 * may be called while the surface is running: `constraintLayout` commits on the calling thread and, because the
 * default commit options mount synchronously, mounts there as well, and the other two copy the scene — and, for
 * `takeFrame`, its accumulated damage — out under the mounting manager's mutex.
 *
 * Shutdown contract: stopSurface commits an empty tree and queues the resulting unmount onto the JavaScript
 * thread. The owner drains that thread before destroying the host, because the queued rendering update holds a
 * raw pointer to the scheduler delegate and upstream only guards it behind a feature flag that is off by default.
 *
 * Input is the other trio the frame thread calls while the surface is running. `dispatchInput` hit-tests and
 * enqueues, `advanceScroll` integrates a frame of scroll physics and writes the resulting `contentOffset` back
 * through a state update, `induceEventBeat` releases everything the queue has accumulated onto the JavaScript
 * thread, and the frame loop calls them in that order once per frame — which is what makes event delivery
 * frame-paced rather than per raw compositor event. See *Input* and *ScrollView* in docs/cpp-toolchain.md.
 */
class FabricHost final {
public:
    FabricHost(facebook::react::ReactInstance& reactInstance, facebook::react::Size surfaceSize);
    FabricHost(const FabricHost&) = delete;
    FabricHost(FabricHost&&) = delete;
    FabricHost& operator=(const FabricHost&) = delete;
    FabricHost& operator=(FabricHost&&) = delete;
    ~FabricHost() noexcept;

    void setSurfaceSize(facebook::react::Size surfaceSize);
    void stopSurface();

    /**
     * Registers the compositor's text input, which focus enables while a text component holds it. The sink is
     * borrowed and must outlive this host; passing null is how a caller that has no text input says so.
     */
    void setTextInputFocusSink(TextInputFocusSink* textInputFocusSink);
    void dispatchInput(const std::vector<InputEvent>& events);

    /**
     * Queues a `focus` `dispatchCommand` for `tag`, the same way a `<View>` ref's `focus()` reaches
     * `InputDispatcher::dispatchCommands` — through `LinuxMountingManager`'s command queue rather than through
     * `dispatchInput`. `LinuxMountingManager::dispatchCommand` reads nothing off the `ShadowView` but its `tag`,
     * so this needs no shadow node at all: it exists for a headless run to synthesise the command a real
     * `dispatchCommand` binding would have produced, exactly as `--focus-click` synthesises the pointer events a
     * real click would have.
     */
    void injectFocusCommand(facebook::react::Tag tag);

    /**
     * Integrates one frame of scrolling and reports whether any `<ScrollView>` is still moving. Called between
     * `dispatchInput` and `induceEventBeat`, so the state write-back and the scroll events this produces ride the
     * same beat as everything else the frame queued.
     *
     * A frame that dispatched an `onScroll` also pushes the animation backend synchronously — upstream's
     * `UIManagerAnimationBackend::trigger` — before it returns, so an `Animated.event` value bound to
     * `contentOffset` is applied in the frame the scroll is applied in rather than in the one after it. This is
     * the only place that pushes it: both frame loops reach it through this call. See *Event-driven Animated* in
     * docs/cpp-toolchain.md.
     */
    bool advanceScroll(double frameMilliseconds);

    /**
     * Advances the focused `<TextInput>`'s caret blink by one frame, and reports whether it changed phase. The
     * blink lives on the frame clock rather than on a timer because there is no timer in the frame path; a
     * headless run never calls it, which is what makes a caret in a golden reproducible.
     */
    bool advanceCaretBlink(double frameMilliseconds);
    void induceEventBeat();

    /**
     * Delivers one frame to the shared animation backend, or nothing when no animation is running. Called once per
     * drawn frame, after the frame's input and before `takeFrame`, so a layout-affecting animated mutation — which
     * commits and mounts synchronously on this thread — is in the scene the same frame paints. See *Animation
     * choreographer* in docs/cpp-toolchain.md.
     */
    void tickAnimations(std::chrono::steady_clock::time_point now);

    /**
     * Whether the mounting manager, the scroll controller or the animation choreographer have anything the frame
     * clock should treat as pending work: a mount that has not been painted yet, a scroll that is still being
     * dragged or gliding, or a running animation. See *Frame clock* in docs/cpp-toolchain.md.
     */
    bool hasPendingWork() const;
    SceneFrame takeFrame();
    SceneSnapshot snapshotScene() const;

    /**
     * The node a press at `surfacePoint` would land on, asked directly rather than through an injected pointer.
     *
     * This is what the hit-versus-paint agreement proof of issue #35 samples: the same answer the input
     * dispatcher would get, taken while the host is alive and compared against the pixels the same scene paints.
     */
    SceneHit findNodeAtPoint(facebook::react::Point surfacePoint) const;
    std::string dumpScene() const;

private:
    std::shared_ptr<const facebook::react::ContextContainer> contextContainer_;
    std::shared_ptr<facebook::react::ComponentDescriptorProviderRegistry> componentDescriptorProviderRegistry_;
    std::shared_ptr<LinuxMountingManager> mountingManager_;
    std::shared_ptr<LinuxAnimationChoreographer> animationChoreographer_;
    std::weak_ptr<facebook::react::UIManagerAnimationBackend> animationBackend_;
    std::function<void()> eventBeatInducer_;
    std::unique_ptr<facebook::react::SchedulerDelegateImpl> schedulerDelegate_;
    std::unique_ptr<facebook::react::Scheduler> scheduler_;
    std::unique_ptr<InputDispatcher> inputDispatcher_;
    std::unique_ptr<ScrollController> scrollController_;
    std::unique_ptr<facebook::react::SurfaceHandler> surfaceHandler_;
};

} // namespace react_native_linux
