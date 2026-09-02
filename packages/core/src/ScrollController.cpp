#include "ScrollController.h"

#include <react/renderer/components/scrollview/ScrollEvent.h>
#include <react/renderer/components/scrollview/ScrollViewEventEmitter.h>
#include <react/renderer/components/scrollview/ScrollViewProps.h>
#include <react/renderer/components/scrollview/ScrollViewState.h>
#include <react/renderer/core/ConcreteState.h>
#include <react/renderer/core/ShadowNode.h>
#include <react/renderer/core/ShadowNodeFamily.h>
#include <react/renderer/core/StateData.h>
#include <react/renderer/graphics/Float.h>
#include <react/renderer/graphics/Point.h>
#include <react/renderer/graphics/Size.h>
#include <react/renderer/mounting/ShadowTree.h>
#include <react/timing/primitives.h>

#include <algorithm>
#include <cstddef>
#include <memory>
#include <utility>
#include <vector>

namespace react_native_linux {

namespace {

// A frame this short cannot have happened and a frame this long is a stall, and integrating either literally
// would turn a hiccup into a jump. The range brackets everything between a 1000 Hz redraw and a tenth of a second.
constexpr double kMinimumFrameMilliseconds = 1.0;
constexpr double kMaximumFrameMilliseconds = 100.0;
constexpr double kMillisecondsPerSecond = 1000.0;

/**
 * What the physics needs to know about a ScrollView this frame, read fresh from the shadow tree because a commit
 * may have resized the viewport or grown the content since the last one.
 */
struct ScrollViewMetrics {
    facebook::react::Size viewportSize;
    facebook::react::Size contentSize;
    double decelerationRate{kDecelerationRateNormal};
};

facebook::react::Point toPoint(double x, double y) {
    return facebook::react::Point{.x = static_cast<facebook::react::Float>(x),
                                  .y = static_cast<facebook::react::Float>(y)};
}

ScrollViewMetrics readMetrics(const facebook::react::ScrollViewShadowNode& scrollView) {
    return ScrollViewMetrics{.viewportSize = scrollView.getLayoutMetrics().frame.size,
                             .contentSize = scrollView.getStateData().getContentSize(),
                             .decelerationRate = scrollView.getConcreteProps().decelerationRate};
}

/**
 * The ScrollView that owns the hit node: the deepest `ScrollViewShadowNode` on the path from the root down to it.
 *
 * `getAncestors` hands that path back as (parent, child index) pairs ordered from the root down, so walking it
 * backwards visits the hit node itself first and each of its ancestors after it, and the first ScrollView found
 * that way is the innermost one.
 */
std::shared_ptr<const facebook::react::ScrollViewShadowNode> deepestScrollView(
    const facebook::react::ShadowNode& hitNode, const facebook::react::ShadowNode& rootNode) {
    const facebook::react::ShadowNodeFamily::AncestorList ancestors = hitNode.getFamily().getAncestors(rootNode);

    for (size_t depth = ancestors.size(); depth > 0; --depth) {
        const facebook::react::ShadowNode& parent = ancestors[depth - 1].first.get();
        const std::shared_ptr<const facebook::react::ScrollViewShadowNode> scrollView =
            std::dynamic_pointer_cast<const facebook::react::ScrollViewShadowNode>(
                parent.getChildren()[static_cast<size_t>(ancestors[depth - 1].second)]);

        if (scrollView != nullptr) {
            return scrollView;
        }
    }

    return nullptr;
}

/**
 * Advances one axis by one frame and clears what it consumed.
 *
 * A finger on a touchpad moves the content one-to-one and leaves the velocity that delta implies behind it, which
 * is what a fling starts from. Everything else decelerates, with any wheel notches folded in first as the velocity
 * whose curve covers exactly their distance — so turning the wheel again mid-glide accelerates, for the same
 * reason two flicks in a row do.
 */
void advanceAxis(ScrollTargetAxis& axis, bool isFingerDown, double frameMilliseconds, double decelerationRate,
                 double contentLength, double viewportLength) {
    if (isFingerDown) {
        axis.state = dragAxis(axis.state, axis.pendingDrag, frameMilliseconds, contentLength, viewportLength);
    } else {
        const ScrollAxisState impulsed{.offset = axis.state.offset,
                                       .velocity = axis.state.velocity +
                                                   velocityForTravel(axis.pendingNotches * kWheelNotchDistance,
                                                                     decelerationRate)};

        axis.state = decelerateAxis(impulsed, frameMilliseconds, decelerationRate, contentLength, viewportLength);
    }

    axis.pendingDrag = 0.0;
    axis.pendingNotches = 0.0;
}

facebook::react::ScrollEvent makeScrollEvent(const ScrollViewMetrics& metrics, facebook::react::Point contentOffset) {
    facebook::react::ScrollEvent scrollEvent;

    scrollEvent.contentSize = metrics.contentSize;
    scrollEvent.contentOffset = contentOffset;
    scrollEvent.containerSize = metrics.viewportSize;
    scrollEvent.zoomScale = 1;
    scrollEvent.timestamp = static_cast<facebook::react::Float>(
        facebook::react::HighResTimeStamp::now().toDOMHighResTimeStamp() / kMillisecondsPerSecond);

    return scrollEvent;
}

/**
 * Publishes the platform's scroll position into the shadow tree.
 *
 * The transforming form of `updateState` is what is called rather than the replacing one: a commit may land
 * between reading the state and applying this update, and only one of the fields in it is ours to move.
 */
void writeContentOffset(const facebook::react::ScrollViewShadowNode& scrollView,
                        facebook::react::Point contentOffset) {
    const std::shared_ptr<const facebook::react::ConcreteState<facebook::react::ScrollViewState>> state =
        std::dynamic_pointer_cast<const facebook::react::ConcreteState<facebook::react::ScrollViewState>>(
            scrollView.getState());

    if (state == nullptr) {
        return;
    }

    state->updateState(
        [contentOffset](const facebook::react::ScrollViewState& previousData) -> facebook::react::StateData::Shared {
            facebook::react::ScrollViewState data = previousData;

            data.contentOffset = contentOffset;

            return std::make_shared<const facebook::react::ScrollViewState>(data);
        });
}

} // namespace

ScrollController::ScrollController(std::shared_ptr<facebook::react::UIManager> uiManager,
                                   facebook::react::SurfaceId surfaceId)
    : uiManager_(std::move(uiManager)), surfaceId_(surfaceId) {}

void ScrollController::dispatch(const std::vector<InputEvent>& events) {
    for (const InputEvent& event : events) {
        if (isScrollEvent(event)) {
            route(event);
        }
    }
}

bool ScrollController::advance(double frameMilliseconds) {
    const double elapsed = std::clamp(frameMilliseconds, kMinimumFrameMilliseconds, kMaximumFrameMilliseconds);
    bool isScrolling = false;

    for (auto entry = targets_.begin(); entry != targets_.end();) {
        ScrollTarget& target = entry->second;
        const std::shared_ptr<const facebook::react::ScrollViewShadowNode> scrollView =
            std::dynamic_pointer_cast<const facebook::react::ScrollViewShadowNode>(
                uiManager_->getNewestCloneOfShadowNode(*target.shadowNode));

        if (scrollView == nullptr) {
            entry = targets_.erase(entry);

            continue;
        }

        target.shadowNode = scrollView;
        isScrolling = advanceTarget(target, *scrollView, elapsed) || isScrolling;
        ++entry;
    }

    return isScrolling;
}

bool ScrollController::isScrollActive() const noexcept {
    for (const auto& entry : targets_) {
        if (entry.second.isFingerDown || entry.second.isMomentumRunning) {
            return true;
        }
    }

    return false;
}

void ScrollController::route(const InputEvent& event) {
    if (event.kind == InputEventKind::PointerScrollStop) {
        // The stop carries no position, and at most one ScrollView can have a finger on it, so releasing every
        // target that has one is the same answer as remembering which one did.
        for (auto& entry : targets_) {
            entry.second.hasReleased = entry.second.hasReleased || entry.second.isFingerDown;
        }

        return;
    }

    ScrollTarget* target = acquire(event.surfacePoint);

    if (target == nullptr) {
        return;
    }

    ScrollTargetAxis& axis = event.scrollAxis == ScrollAxisKind::Horizontal ? target->horizontal : target->vertical;

    if (event.kind == InputEventKind::PointerScrollDiscrete) {
        axis.pendingNotches += event.scrollAmount;

        return;
    }

    axis.pendingDrag += event.scrollAmount;
    target->isFingerDown = true;
}

ScrollController::ScrollTarget* ScrollController::acquire(facebook::react::Point surfacePoint) {
    const std::shared_ptr<const facebook::react::ScrollViewShadowNode> scrollView =
        scrollViewUnderPointer(surfacePoint);

    if (scrollView == nullptr || !scrollView->getConcreteProps().scrollEnabled) {
        return nullptr;
    }

    const auto existing = targets_.find(scrollView->getTag());

    if (existing != targets_.end()) {
        return &existing->second;
    }

    // Seeded from the state, so a ScrollView that JavaScript mounted at a non-zero `contentOffset` keeps it, and
    // so does one this controller already scrolled before its entry was dropped.
    const facebook::react::Point contentOffset = scrollView->getStateData().contentOffset;
    const ScrollTarget target{
        .shadowNode = scrollView,
        .horizontal = ScrollTargetAxis{.state = ScrollAxisState{.offset = contentOffset.x}},
        .vertical = ScrollTargetAxis{.state = ScrollAxisState{.offset = contentOffset.y}}};

    return &targets_.emplace(scrollView->getTag(), target).first->second;
}

bool ScrollController::advanceTarget(ScrollTarget& target, const facebook::react::ScrollViewShadowNode& scrollView,
                                     double frameMilliseconds) {
    const ScrollViewMetrics metrics = readMetrics(scrollView);
    const facebook::react::Point previousOffset =
        toPoint(target.horizontal.state.offset, target.vertical.state.offset);
    const bool wasMomentumRunning = target.isMomentumRunning;

    advanceAxis(target.horizontal, target.isFingerDown, frameMilliseconds, metrics.decelerationRate,
                metrics.contentSize.width, metrics.viewportSize.width);
    advanceAxis(target.vertical, target.isFingerDown, frameMilliseconds, metrics.decelerationRate,
                metrics.contentSize.height, metrics.viewportSize.height);

    // The release is applied after the frame it arrived in has been dragged, so the velocity the last delta left
    // behind is the velocity the fling starts with.
    if (target.hasReleased) {
        target.isFingerDown = false;
        target.hasReleased = false;
    }

    target.isMomentumRunning = !target.isFingerDown && (target.horizontal.state.velocity != 0.0 ||
                                                        target.vertical.state.velocity != 0.0);

    const facebook::react::Point contentOffset =
        toPoint(target.horizontal.state.offset, target.vertical.state.offset);
    const bool hasMoved = contentOffset != previousOffset;

    if (hasMoved) {
        writeContentOffset(scrollView, contentOffset);
    }

    const std::shared_ptr<const facebook::react::ScrollViewEventEmitter> emitter =
        std::dynamic_pointer_cast<const facebook::react::ScrollViewEventEmitter>(scrollView.getEventEmitter());

    if (emitter != nullptr) {
        const facebook::react::ScrollEvent scrollEvent = makeScrollEvent(metrics, contentOffset);

        if (!wasMomentumRunning && target.isMomentumRunning) {
            emitter->onMomentumScrollBegin(scrollEvent);
        }

        if (hasMoved) {
            emitter->onScroll(scrollEvent);
        }

        if (wasMomentumRunning && !target.isMomentumRunning) {
            emitter->onMomentumScrollEnd(scrollEvent);
        }
    }

    return target.isFingerDown || target.isMomentumRunning;
}

std::shared_ptr<const facebook::react::ScrollViewShadowNode> ScrollController::scrollViewUnderPointer(
    facebook::react::Point surfacePoint) const {
    std::shared_ptr<const facebook::react::ShadowNode> rootNode;

    uiManager_->getShadowTreeRegistry().visit(surfaceId_, [&rootNode](const facebook::react::ShadowTree& tree) {
        rootNode = tree.getCurrentRevision().rootShadowNode;
    });

    if (rootNode == nullptr) {
        return nullptr;
    }

    const std::shared_ptr<const facebook::react::ShadowNode> hitNode =
        uiManager_->findNodeAtPoint(rootNode, surfacePoint);

    if (hitNode == nullptr) {
        return nullptr;
    }

    return deepestScrollView(*hitNode, *rootNode);
}

} // namespace react_native_linux
