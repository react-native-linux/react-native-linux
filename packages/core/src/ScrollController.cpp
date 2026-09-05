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
    double scrollEventThrottleMilliseconds{0.0};
    ScrollSnapConfiguration snapping;
};

ScrollSnapAlignment toSnapAlignment(facebook::react::ScrollViewSnapToAlignment alignment) {
    if (alignment == facebook::react::ScrollViewSnapToAlignment::Center) {
        return ScrollSnapAlignment::Center;
    }

    if (alignment == facebook::react::ScrollViewSnapToAlignment::End) {
        return ScrollSnapAlignment::End;
    }

    return ScrollSnapAlignment::Start;
}

/**
 * The snap props, read for both axes rather than for the scrolling one. `horizontal` is not modelled — see
 * *ScrollView* in docs/cpp-toolchain.md — and it does not need to be: an axis with nothing to scroll has a single
 * snap point at zero, which is where it already is.
 */
ScrollSnapConfiguration readSnapping(const facebook::react::ScrollViewProps& props) {
    return ScrollSnapConfiguration{.interval = props.snapToInterval,
                                   .offsets = std::vector<double>(props.snapToOffsets.begin(),
                                                                  props.snapToOffsets.end()),
                                   .alignment = toSnapAlignment(props.snapToAlignment),
                                   .snapToStart = props.snapToStart,
                                   .snapToEnd = props.snapToEnd,
                                   .isPagingEnabled = props.pagingEnabled,
                                   .isIntervalMomentumDisabled = props.disableIntervalMomentum};
}

facebook::react::Point toPoint(double x, double y) {
    return facebook::react::Point{.x = static_cast<facebook::react::Float>(x),
                                  .y = static_cast<facebook::react::Float>(y)};
}

ScrollViewMetrics readMetrics(const facebook::react::ScrollViewShadowNode& scrollView) {
    return ScrollViewMetrics{.viewportSize = scrollView.getLayoutMetrics().frame.size,
                             .contentSize = scrollView.getStateData().getContentSize(),
                             .decelerationRate = scrollView.getConcreteProps().decelerationRate,
                             .scrollEventThrottleMilliseconds = scrollView.getConcreteProps().scrollEventThrottle,
                             .snapping = readSnapping(scrollView.getConcreteProps())};
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
 * Re-aims a glide at the snap point it is going to settle on, by replacing its velocity with the one whose curve
 * covers exactly the distance to that point — the inversion a wheel notch already uses, so snapping introduces no
 * second motion model and keeps the ordinary momentum bracket.
 *
 * It runs every frame rather than once at the release because that is a fixed point rather than a repetition: the
 * analytic landing point of a decelerating velocity does not change as the curve is walked, so every later frame
 * re-chooses the snap point the first one aimed at.
 *
 * `isSettlingFromRelease` is the one frame a motionless release owns. `UIScrollView` pages on any release, not
 * only on a flick, so a finger that stops before it lifts still has to align — but an idle ScrollView and the
 * offset an unanimated `scrollTo` just wrote must not be dragged off by the same rule, and neither of those is a
 * release. A moving axis needs no flag: its velocity is already the gesture.
 */
ScrollAxisState aimAtSnapPoint(const ScrollAxisState& axis, bool isSettlingFromRelease, double decelerationRate,
                               double contentLength, double viewportLength,
                               const ScrollSnapConfiguration& snapping) {
    if ((axis.velocity == 0.0 && !isSettlingFromRelease) || !hasSnapPoints(snapping)) {
        return axis;
    }

    const double target = settleTargetOffset(axis, decelerationRate, contentLength, viewportLength, snapping);

    return ScrollAxisState{.offset = axis.offset,
                           .velocity = velocityForTravel(target - axis.offset, decelerationRate)};
}

/**
 * Advances one axis by one frame and clears what it consumed.
 *
 * A finger on a touchpad moves the content one-to-one and leaves the velocity that delta implies behind it, which
 * is what a fling starts from. Everything else decelerates, with any wheel notches folded in first as the velocity
 * whose curve covers exactly their distance — so turning the wheel again mid-glide accelerates, for the same
 * reason two flicks in a row do.
 */
void advanceAxis(ScrollTargetAxis& axis, bool isFingerDown, bool isSettlingFromRelease, double frameMilliseconds,
                 double decelerationRate, double contentLength, double viewportLength,
                 const ScrollSnapConfiguration& snapping) {
    if (axis.pendingOffset.has_value()) {
        axis.state = ScrollAxisState{.offset = axis.pendingOffset.value()};
        axis.pendingOffset.reset();
    }

    if (axis.pendingVelocity.has_value()) {
        axis.state.velocity = axis.pendingVelocity.value();
        axis.pendingVelocity.reset();
    }

    if (isFingerDown) {
        axis.state = dragAxis(axis.state, axis.pendingDrag, frameMilliseconds, contentLength, viewportLength);
    } else {
        const ScrollAxisState impulsed{.offset = axis.state.offset,
                                       .velocity = axis.state.velocity +
                                                   velocityForTravel(axis.pendingNotches * kWheelNotchDistance,
                                                                     decelerationRate)};

        const ScrollAxisState aimed = aimAtSnapPoint(impulsed, isSettlingFromRelease, decelerationRate,
                                                     contentLength, viewportLength, snapping);

        axis.state = decelerateAxis(aimed, frameMilliseconds, decelerationRate, contentLength, viewportLength);
    }

    axis.pendingDrag = 0.0;
    axis.pendingNotches = 0.0;
}

/**
 * One positional argument of a `dispatchCommand` payload, or a default. A command whose arguments are missing or
 * of the wrong type scrolls to the origin rather than failing: it arrives from JavaScript, and the frame thread
 * is not where a third-party library's mistake should be fatal.
 */
double readNumberArgument(const folly::dynamic& args, size_t index) {
    if (!args.isArray() || index >= args.size() || !args[index].isNumber()) {
        return 0.0;
    }

    return args[index].asDouble();
}

bool readBooleanArgument(const folly::dynamic& args, size_t index) {
    if (!args.isArray() || index >= args.size() || !args[index].isBool()) {
        return false;
    }

    return args[index].asBool();
}

void applyDestination(ScrollTargetAxis& axis, const ScrollDestination& destination) {
    if (!destination.hasWork) {
        return;
    }

    if (destination.velocity == 0.0) {
        axis.pendingOffset = destination.offset;

        return;
    }

    axis.pendingVelocity = destination.velocity;
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

    hasDispatchedScrollEvent_ = false;

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

bool ScrollController::hasDispatchedScrollEvent() const noexcept { return hasDispatchedScrollEvent_; }

bool ScrollController::isScrollActive() const noexcept {
    for (const auto& entry : targets_) {
        const ScrollTarget& target = entry.second;
        const bool hasPendingCommand = target.horizontal.pendingOffset.has_value() ||
                                       target.horizontal.pendingVelocity.has_value() ||
                                       target.vertical.pendingOffset.has_value() ||
                                       target.vertical.pendingVelocity.has_value();

        if (target.isFingerDown || target.isMomentumRunning || target.isSettlingFromRelease || hasPendingCommand) {
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

void ScrollController::dispatchCommands(const std::vector<SceneCommand>& commands) {
    for (const SceneCommand& command : commands) {
        routeCommand(command);
    }
}

/**
 * One `scrollTo(x, y, animated)` or `scrollToEnd(animated)`, turned into what the next frame should do.
 *
 * The arguments are read positionally out of the `folly::dynamic` array upstream's `ScrollViewNativeComponent`
 * sends, and anything missing reads as zero or false rather than failing the command: a malformed command from a
 * third-party library should scroll to the origin, not take the frame thread down.
 */
void ScrollController::routeCommand(const SceneCommand& command) {
    const bool isScrollTo = command.name == "scrollTo";

    if (!isScrollTo && command.name != "scrollToEnd") {
        return;
    }

    const std::shared_ptr<const facebook::react::ScrollViewShadowNode> scrollView = scrollViewWithTag(command.tag);

    if (scrollView == nullptr) {
        return;
    }

    ScrollTarget* target = acquireNode(scrollView);

    if (target == nullptr) {
        return;
    }

    const ScrollViewMetrics metrics = readMetrics(*scrollView);
    const bool isAnimated = isScrollTo ? readBooleanArgument(command.args, 2) : readBooleanArgument(command.args, 0);
    const double targetX = isScrollTo ? readNumberArgument(command.args, 0)
                                      : maximumScrollOffset(metrics.contentSize.width, metrics.viewportSize.width);
    const double targetY = isScrollTo
                               ? readNumberArgument(command.args, 1)
                               : maximumScrollOffset(metrics.contentSize.height, metrics.viewportSize.height);

    applyDestination(target->horizontal,
                     scrollToDestination(target->horizontal.state.offset, targetX, isAnimated,
                                         metrics.decelerationRate, metrics.contentSize.width,
                                         metrics.viewportSize.width));
    applyDestination(target->vertical,
                     scrollToDestination(target->vertical.state.offset, targetY, isAnimated,
                                         metrics.decelerationRate, metrics.contentSize.height,
                                         metrics.viewportSize.height));
}

ScrollController::ScrollTarget* ScrollController::acquireNode(
    const std::shared_ptr<const facebook::react::ScrollViewShadowNode>& scrollView) {
    if (!scrollView->getConcreteProps().scrollEnabled) {
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

ScrollController::ScrollTarget* ScrollController::acquire(facebook::react::Point surfacePoint) {
    const std::shared_ptr<const facebook::react::ScrollViewShadowNode> scrollView =
        scrollViewUnderPointer(surfacePoint);

    return scrollView == nullptr ? nullptr : acquireNode(scrollView);
}

bool ScrollController::advanceTarget(ScrollTarget& target, const facebook::react::ScrollViewShadowNode& scrollView,
                                     double frameMilliseconds) {
    const ScrollViewMetrics metrics = readMetrics(scrollView);
    const facebook::react::Point previousOffset =
        toPoint(target.horizontal.state.offset, target.vertical.state.offset);

    const bool isSettlingFromRelease = target.isSettlingFromRelease;

    target.isSettlingFromRelease = false;

    advanceAxis(target.horizontal, target.isFingerDown, isSettlingFromRelease, frameMilliseconds,
                metrics.decelerationRate, metrics.contentSize.width, metrics.viewportSize.width, metrics.snapping);
    advanceAxis(target.vertical, target.isFingerDown, isSettlingFromRelease, frameMilliseconds,
                metrics.decelerationRate, metrics.contentSize.height, metrics.viewportSize.height,
                metrics.snapping);

    // The release is applied after the frame it arrived in has been dragged, so the velocity the last delta left
    // behind is the velocity the fling starts with.
    if (target.hasReleased) {
        target.isFingerDown = false;
        target.hasReleased = false;
        target.isSettlingFromRelease = hasSnapPoints(metrics.snapping);
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

    const ScrollCadenceEvents cadenceEvents = target.cadence.advance(
        ScrollCadenceFrame{.frameMilliseconds = frameMilliseconds,
                           .throttleMilliseconds = metrics.scrollEventThrottleMilliseconds,
                           .hasMoved = hasMoved,
                           .isDragging = target.isFingerDown,
                           .isMomentumRunning = target.isMomentumRunning});

    if (emitter != nullptr) {
        const facebook::react::ScrollEvent scrollEvent = makeScrollEvent(metrics, contentOffset);

        if (cadenceEvents.beginDrag) {
            emitter->onScrollBeginDrag(scrollEvent);
        }

        if (cadenceEvents.scroll) {
            emitter->onScroll(scrollEvent);
            hasDispatchedScrollEvent_ = true;
        }

        if (cadenceEvents.endDrag) {
            emitter->onScrollEndDrag(scrollEvent);
        }

        if (cadenceEvents.momentumBegin) {
            emitter->onMomentumScrollBegin(scrollEvent);
        }

        if (cadenceEvents.momentumEnd) {
            emitter->onMomentumScrollEnd(scrollEvent);
        }
    }

    return target.isFingerDown || target.isMomentumRunning || target.isSettlingFromRelease;
}

namespace {

/**
 * The `<ScrollView>` with this tag anywhere under `node`, or nothing. A command names a tag rather than a point,
 * so this is the lookup `scrollViewUnderPointer` is for a press.
 */
std::shared_ptr<const facebook::react::ScrollViewShadowNode> findScrollViewWithTag(
    const std::shared_ptr<const facebook::react::ShadowNode>& node, facebook::react::Tag tag) {
    if (node->getTag() == tag) {
        return std::dynamic_pointer_cast<const facebook::react::ScrollViewShadowNode>(node);
    }

    for (const std::shared_ptr<const facebook::react::ShadowNode>& child : node->getChildren()) {
        const std::shared_ptr<const facebook::react::ScrollViewShadowNode> found = findScrollViewWithTag(child, tag);

        if (found != nullptr) {
            return found;
        }
    }

    return nullptr;
}

} // namespace

/**
 * The surface's current root, which both lookups below start from: one walks it for a tag and the other hit-tests
 * it for a point.
 */
std::shared_ptr<const facebook::react::ShadowNode> ScrollController::rootShadowNode() const {
    std::shared_ptr<const facebook::react::ShadowNode> rootNode;

    uiManager_->getShadowTreeRegistry().visit(surfaceId_, [&rootNode](const facebook::react::ShadowTree& tree) {
        rootNode = tree.getCurrentRevision().rootShadowNode;
    });

    return rootNode;
}

std::shared_ptr<const facebook::react::ScrollViewShadowNode> ScrollController::scrollViewWithTag(
    facebook::react::Tag tag) const {
    const std::shared_ptr<const facebook::react::ShadowNode> rootNode = rootShadowNode();

    return rootNode == nullptr ? nullptr : findScrollViewWithTag(rootNode, tag);
}

std::shared_ptr<const facebook::react::ScrollViewShadowNode> ScrollController::scrollViewUnderPointer(
    facebook::react::Point surfacePoint) const {
    const std::shared_ptr<const facebook::react::ShadowNode> rootNode = rootShadowNode();

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
