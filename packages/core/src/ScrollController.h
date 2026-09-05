#pragma once

#include "InputPipeline.h"
#include "LinuxMountingManager.h"
#include "ScrollEventCadence.h"
#include "ScrollPhysics.h"

#include <react/renderer/components/scrollview/ScrollViewShadowNode.h>
#include <react/renderer/core/ReactPrimitives.h>
#include <react/renderer/graphics/Point.h>
#include <react/renderer/uimanager/UIManager.h>

#include <memory>
#include <optional>
#include <unordered_map>
#include <vector>

namespace react_native_linux {

/**
 * One axis of one `<ScrollView>`: the position and velocity the physics owns, plus what arrived for it since the
 * last frame.
 *
 * Deltas and notches are separate because they mean different things. A touchpad delta is displacement that has
 * already happened and moves the content one-to-one; a wheel notch is a velocity impulse whose distance is still
 * ahead of it.
 */
struct ScrollTargetAxis {
    ScrollAxisState state;
    double pendingDrag{0.0};
    double pendingNotches{0.0};

    /**
     * What a `scrollTo` command left for the next frame: an offset to jump to, a velocity to glide with, or
     * neither. It is pending rather than applied because `advanceTarget` reads the offset it is going to compare
     * against *before* it advances — a jump written straight into the state would already be the previous offset
     * by the time the frame looked, and would emit no `onScroll` for a scroll that did move.
     */
    std::optional<double> pendingOffset;
    std::optional<double> pendingVelocity;
};

/**
 * The platform half of `<ScrollView>`: it owns the scroll position, integrates the deceleration curve once per
 * frame, and publishes the result back into Fabric.
 *
 * React Native has no cross-platform scrolling. `ScrollViewShadowNode` lays children out relative to the
 * ScrollView and never moves them; the position lives in `ScrollViewState::contentOffset`, and every platform is
 * expected to drive that number itself and to emit the scroll events from it. That is what this class is, and it
 * is deliberately shaped like `RCTScrollViewComponentView`'s sequence: move, write the state, emit `onScroll`,
 * bracket the drag with `onScrollBeginDrag` and `onScrollEndDrag` and the glide with `onMomentumScrollBegin` and
 * `onMomentumScrollEnd`. Which of those five a frame emits is `ScrollEventCadence`, which is the whole of the
 * cadence contract of issue #45 and is arithmetic, so it lives inside the coverage gate rather than here.
 *
 * The offset the platform holds is authoritative between commits, seeded from the state the first time a
 * ScrollView is scrolled. Writing it back through `ConcreteState::updateState` rather than reading it back is what
 * keeps a fling smooth: the write is queued and applied on the JavaScript thread at the next event beat, so a
 * controller that trusted the mounted value would stall for a frame every time that thread was busy.
 *
 * Routing is a hit test plus one walk up. `UIManager::findNodeAtPoint` answers what is under the pointer, exactly
 * as it does for a click, and the deepest `ScrollViewShadowNode` on the path from the root to that node is what a
 * wheel over it scrolls — which is nested-ScrollView behaviour without a second rule for it.
 *
 * Threading contract: every member runs on the platform frame thread. `findNodeAtPoint`,
 * `getNewestCloneOfShadowNode` and `ShadowNodeFamily::getAncestors` all take the shadow tree's shared lock and are
 * documented as callable from any thread; `ConcreteState::updateState` and the event emitters enqueue under their
 * own locks and are flushed by whoever induces the beat.
 */
class ScrollController final {
public:
    ScrollController(std::shared_ptr<facebook::react::UIManager> uiManager, facebook::react::SurfaceId surfaceId);

    /**
     * Routes one frame's scroll input. Events that are not scroll events are ignored, so the caller may hand over
     * the whole frame.
     */
    void dispatch(const std::vector<InputEvent>& events);

    /**
     * Applies the frame's `dispatchCommand` queue. Commands that are not `scrollTo` or `scrollToEnd`, and
     * commands naming a node that is not a `<ScrollView>`, are ignored, so the caller may hand over the whole
     * queue exactly as it does with input.
     *
     * A command that asks for the offset the content is already at does nothing at all, which is the rule
     * core#34327 landed on: it must not emit `onScroll` and must not open a momentum bracket. See
     * `scrollToDestination`.
     */
    void dispatchCommands(const std::vector<SceneCommand>& commands);

    /**
     * Integrates one frame and returns whether anything is still moving, which is what lets a headless run know
     * when a fling has settled. `frameMilliseconds` is confined to a plausible range, so a stalled frame slows the
     * scroll down instead of teleporting it.
     */
    bool advance(double frameMilliseconds);

    /**
     * Whether the last `advance` dispatched an `onScroll`. That is the frame an `Animated.event` value bound to
     * `contentOffset` can have changed in, and therefore the frame the animated graph has to be pushed through
     * before anything is painted. See *Event-driven Animated* in docs/cpp-toolchain.md.
     */
    bool hasDispatchedScrollEvent() const noexcept;

    /**
     * Whether any target is still being dragged or gliding, as of the last `advance`. Peeking this costs no
     * `UIManager` lookup, unlike `advance` itself, which is what makes it usable as the frame clock's
     * fallback-timeout pending-work signal (see *Frame clock* in docs/cpp-toolchain.md) without advancing physics
     * twice for the same frame.
     */
    bool isScrollActive() const noexcept;

private:
    struct ScrollTarget {
        std::shared_ptr<const facebook::react::ScrollViewShadowNode> shadowNode;
        ScrollTargetAxis horizontal;
        ScrollTargetAxis vertical;
        ScrollEventCadence cadence;
        bool isFingerDown{false};
        bool hasReleased{false};
        bool isMomentumRunning{false};

        /**
         * The one frame after a finger lifts, during which a snapping ScrollView aligns whether or not the
         * release carried any velocity. See `aimAtSnapPoint`.
         */
        bool isSettlingFromRelease{false};
    };

    void route(const InputEvent& event);
    void routeCommand(const SceneCommand& command);
    ScrollTarget* acquire(facebook::react::Point surfacePoint);
    ScrollTarget* acquireNode(const std::shared_ptr<const facebook::react::ScrollViewShadowNode>& scrollView);
    std::shared_ptr<const facebook::react::ScrollViewShadowNode> scrollViewWithTag(facebook::react::Tag tag) const;
    std::shared_ptr<const facebook::react::ShadowNode> rootShadowNode() const;
    bool advanceTarget(ScrollTarget& target, const facebook::react::ScrollViewShadowNode& scrollView,
                       double frameMilliseconds);
    std::shared_ptr<const facebook::react::ScrollViewShadowNode> scrollViewUnderPointer(
        facebook::react::Point surfacePoint) const;

    std::shared_ptr<facebook::react::UIManager> uiManager_;
    facebook::react::SurfaceId surfaceId_;
    std::unordered_map<facebook::react::Tag, ScrollTarget> targets_;
    bool hasDispatchedScrollEvent_{false};
};

} // namespace react_native_linux
