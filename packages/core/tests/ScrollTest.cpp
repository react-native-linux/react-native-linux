#include "InputPipeline.h"
#include "ScrollController.h"
#include "ScrollEventCadence.h"
#include "ScrollPhysics.h"
#include "ShadowTreeTestSupport.h"

#include <LinuxMountingManager.h>
#include <cstddef>
#include <gtest/gtest.h>
#include <react/renderer/components/scrollview/ScrollViewComponentDescriptor.h>
#include <react/renderer/components/scrollview/ScrollViewShadowNode.h>
#include <react/renderer/components/view/ViewShadowNode.h>
#include <react/renderer/core/LayoutConstraints.h>
#include <react/renderer/core/LayoutContext.h>
#include <react/renderer/core/PropsParserContext.h>
#include <react/renderer/core/ShadowNodeFamily.h>
#include <react/renderer/core/ShadowNodeFragment.h>
#include <react/renderer/graphics/Point.h>
#include <react/renderer/mounting/ShadowTree.h>
#include <react/renderer/uimanager/UIManager.h>
#include <vector>

namespace {

using facebook::react::ScrollViewComponentDescriptor;
using facebook::react::UIManager;
using react_native_linux::clampScrollOffset;
using react_native_linux::decelerateAxis;
using react_native_linux::dragAxis;
using react_native_linux::InputEvent;
using react_native_linux::InputEventKind;
using react_native_linux::InputQueue;
using react_native_linux::isScrollEvent;
using react_native_linux::kDecelerationRateFast;
using react_native_linux::kDecelerationRateNormal;
using react_native_linux::kWheelNotchDistance;
using react_native_linux::maximumScrollOffset;
using react_native_linux::PassThroughShadowTreeDelegate;
using react_native_linux::PointerDispatch;
using react_native_linux::PointerRouter;
using react_native_linux::SceneCommand;
using react_native_linux::ScrollAxisKind;
using react_native_linux::ScrollAxisState;
using react_native_linux::ScrollCadenceEvents;
using react_native_linux::ScrollCadenceFrame;
using react_native_linux::ScrollController;
using react_native_linux::ScrollDestination;
using react_native_linux::ScrollEventCadence;
using react_native_linux::scrollToDestination;
using react_native_linux::velocityForTravel;

constexpr SurfaceId kSurfaceId = 1;
using ChildList = std::vector<std::shared_ptr<const ShadowNode>>;

// One 60 Hz frame and one 120 Hz frame, in milliseconds.
constexpr double kFrameMilliseconds60Hz = 1000.0 / 60.0;
constexpr double kFrameMilliseconds120Hz = 1000.0 / 120.0;

// The per-frame decay factors the per-millisecond constants work out as at 60 Hz. These are the numbers
// *ScrollView* in docs/cpp-toolchain.md quotes, asserted here so the documented mapping is a test rather than a
// claim.
constexpr double kNormalDecayPerFrame60Hz = 0.967184;
constexpr double kFastDecayPerFrame60Hz = 0.845771;
constexpr double kDecayTolerance = 1e-4;

// Longer than any fling in this file travels, so nothing clamps unless the test is about clamping.
constexpr double kUnboundedContent = 1e6;
constexpr double kViewportLength = 150.0;

// The fixture's real geometry: 470 points of content in a 150 point viewport leaves 320 points to scroll.
constexpr double kContentLength = 470.0;
constexpr double kMaximumOffset = 320.0;

// Longer than the slowest curve `decelerationRate` can ask for takes to come to rest.
constexpr size_t kSettleFrameLimit = 20000;

/**
 * Where a fling of `velocity` ends up, integrated one frame at a time until the physics reports it stopped.
 */
double settledOffset(double velocity, double frameMilliseconds, double decelerationRate) {
    ScrollAxisState axis{.offset = 0.0, .velocity = velocity};

    for (size_t frame = 0; frame < kSettleFrameLimit && axis.velocity != 0.0; ++frame) {
        axis = decelerateAxis(axis, frameMilliseconds, decelerationRate, kUnboundedContent, kViewportLength);
    }

    return axis.offset;
}

InputEvent makeScroll(InputEventKind kind, ScrollAxisKind scrollAxis, double amount) {
    return InputEvent{.kind = kind, .scrollAxis = scrollAxis, .scrollAmount = amount};
}

// Issue #45. The cadence is what `VirtualizedList` windowing is written against, so each of these is a sentence
// from that contract: a drag is bracketed once, momentum brackets itself inside it, the throttle bounds how often
// `onScroll` arrives without ever dropping the newest offset, and a boundary always reports where it ended.

ScrollCadenceFrame movingFrame(bool isDragging, bool isMomentumRunning, double throttleMilliseconds = 0.0) {
    return ScrollCadenceFrame{.frameMilliseconds = kFrameMilliseconds60Hz,
                              .throttleMilliseconds = throttleMilliseconds,
                              .hasMoved = true,
                              .isDragging = isDragging,
                              .isMomentumRunning = isMomentumRunning};
}

TEST(ScrollEventCadenceTest, AnUnthrottledFrameThatMovedReportsItAndOneThatDidNotDoesNot) {
    ScrollEventCadence cadence;
    const ScrollCadenceEvents moved = cadence.advance(movingFrame(true, false));

    EXPECT_TRUE(moved.beginDrag);
    EXPECT_TRUE(moved.scroll);

    ScrollCadenceFrame still = movingFrame(true, false);

    still.hasMoved = false;

    const ScrollCadenceEvents settled = cadence.advance(still);

    EXPECT_FALSE(settled.beginDrag);
    EXPECT_FALSE(settled.scroll);
}

TEST(ScrollEventCadenceTest, ADragIsBracketedExactlyOnceAndTheReleaseEndsItBeforeMomentumBegins) {
    ScrollEventCadence cadence;
    const ScrollCadenceEvents first = cadence.advance(movingFrame(true, false));
    const ScrollCadenceEvents second = cadence.advance(movingFrame(true, false));

    EXPECT_TRUE(first.beginDrag);
    EXPECT_FALSE(second.beginDrag);
    EXPECT_FALSE(first.endDrag);

    // The release: the finger is up and the fling has started, in one frame. Both are reported, and the struct
    // names them in the order they are emitted — the drag ends before the momentum begins.
    const ScrollCadenceEvents released = cadence.advance(movingFrame(false, true));

    EXPECT_TRUE(released.endDrag);
    EXPECT_TRUE(released.momentumBegin);
    EXPECT_TRUE(released.scroll);

    const ScrollCadenceEvents gliding = cadence.advance(movingFrame(false, true));

    EXPECT_FALSE(gliding.endDrag);
    EXPECT_FALSE(gliding.momentumBegin);

    ScrollCadenceFrame stopped = movingFrame(false, false);

    stopped.hasMoved = false;

    const ScrollCadenceEvents rest = cadence.advance(stopped);

    EXPECT_TRUE(rest.momentumEnd);
    EXPECT_FALSE(rest.scroll);
}

TEST(ScrollEventCadenceTest, TheThrottleIsAMinimumIntervalRatherThanASamplingRate) {
    constexpr double kThrottleMilliseconds = 50.0;
    ScrollEventCadence cadence;

    // 16.67 ms a frame against a 50 ms throttle: the first frame is under the interval and the third crosses it.
    EXPECT_FALSE(cadence.advance(movingFrame(true, false, kThrottleMilliseconds)).scroll);
    EXPECT_FALSE(cadence.advance(movingFrame(true, false, kThrottleMilliseconds)).scroll);
    EXPECT_TRUE(cadence.advance(movingFrame(true, false, kThrottleMilliseconds)).scroll);

    // And the interval starts again from the frame that reported, not from the one that moved.
    EXPECT_FALSE(cadence.advance(movingFrame(true, false, kThrottleMilliseconds)).scroll);
}

TEST(ScrollEventCadenceTest, AFrameThatMovedInsideTheIntervalIsFoldedIntoTheNextReport) {
    constexpr double kThrottleMilliseconds = 50.0;
    ScrollEventCadence cadence;
    ScrollCadenceFrame still = movingFrame(true, false, kThrottleMilliseconds);

    still.hasMoved = false;

    EXPECT_FALSE(cadence.advance(movingFrame(true, false, kThrottleMilliseconds)).scroll);

    // Nothing moves for two frames, but the movement from the first is still unreported when the interval
    // elapses, so it is reported then rather than lost.
    EXPECT_FALSE(cadence.advance(still).scroll);
    EXPECT_TRUE(cadence.advance(still).scroll);
}

TEST(ScrollEventCadenceTest, AThrottledDragReportsWhereItEndedWhateverTheIntervalSays) {
    constexpr double kThrottleMilliseconds = 1000.0;
    ScrollEventCadence cadence;

    EXPECT_FALSE(cadence.advance(movingFrame(true, false, kThrottleMilliseconds)).scroll);

    const ScrollCadenceEvents released = cadence.advance(movingFrame(false, false, kThrottleMilliseconds));

    EXPECT_TRUE(released.endDrag);
    EXPECT_TRUE(released.scroll);
}

TEST(ScrollEventCadenceTest, AThrottledFlingReportsWhereItStopped) {
    constexpr double kThrottleMilliseconds = 1000.0;
    ScrollEventCadence cadence;

    EXPECT_TRUE(cadence.advance(movingFrame(false, true, kThrottleMilliseconds)).momentumBegin);
    EXPECT_FALSE(cadence.advance(movingFrame(false, true, kThrottleMilliseconds)).scroll);

    const ScrollCadenceEvents stopped = cadence.advance(movingFrame(false, false, kThrottleMilliseconds));

    EXPECT_TRUE(stopped.momentumEnd);
    EXPECT_TRUE(stopped.scroll);
}

// Issue #109, and the rule core#34327 landed: a `scrollTo` to where the content already is is not a scroll. The
// geometry is the fixture's own — 470 points of content in a 150 point viewport — so the offset range is
// [0, kMaximumOffset].

ScrollDestination destinationFor(double currentOffset, double targetOffset, bool isAnimated = false) {
    return scrollToDestination(currentOffset, targetOffset, isAnimated, kDecelerationRateNormal, kContentLength,
                               kViewportLength);
}

TEST(ScrollToDestinationTest, ScrollingToWhereTheContentAlreadyIsIsNoWorkAndEmitsNothing) {
    EXPECT_FALSE(destinationFor(120.0, 120.0).hasWork);
    EXPECT_FALSE(destinationFor(120.0, 120.0, true).hasWork);
}

TEST(ScrollToDestinationTest, ATargetPastEitherEndIsClampedBeforeItIsReturned) {
    // core#41034: the negative offset reached JavaScript because the clamp came after the event.
    const ScrollDestination beforeTheStart = destinationFor(120.0, -500.0);
    const ScrollDestination pastTheEnd = destinationFor(120.0, 5000.0);

    EXPECT_TRUE(beforeTheStart.hasWork);
    EXPECT_DOUBLE_EQ(beforeTheStart.offset, 0.0);
    EXPECT_TRUE(pastTheEnd.hasWork);
    EXPECT_DOUBLE_EQ(pastTheEnd.offset, kMaximumOffset);
}

TEST(ScrollToDestinationTest, AClampedTargetThatLandsOnTheCurrentOffsetIsStillNoWork) {
    // Already at the end, asked to go further: the clamp makes it the offset it is already at.
    EXPECT_FALSE(destinationFor(kMaximumOffset, 5000.0).hasWork);
    EXPECT_FALSE(destinationFor(0.0, -1.0).hasWork);
}

TEST(ScrollToDestinationTest, ContentShorterThanTheViewportSettlesAtZero) {
    const ScrollDestination destination =
        scrollToDestination(0.0, 300.0, false, kDecelerationRateNormal, 100.0, kViewportLength);

    EXPECT_FALSE(destination.hasWork);
}

TEST(ScrollToDestinationTest, AnUnanimatedScrollArrivesImmediatelyAndCarriesNoVelocity) {
    const ScrollDestination destination = destinationFor(120.0, 300.0);

    EXPECT_TRUE(destination.hasWork);
    EXPECT_DOUBLE_EQ(destination.offset, 300.0);
    EXPECT_DOUBLE_EQ(destination.velocity, 0.0);
}

TEST(ScrollToDestinationTest, AnAnimatedScrollLeavesTheOffsetAloneAndFlicksTowardsTheTarget) {
    const ScrollDestination forwards = destinationFor(120.0, 300.0, true);
    const ScrollDestination backwards = destinationFor(300.0, 120.0, true);

    EXPECT_DOUBLE_EQ(forwards.offset, 120.0);
    EXPECT_DOUBLE_EQ(forwards.velocity, velocityForTravel(180.0, kDecelerationRateNormal));
    EXPECT_DOUBLE_EQ(backwards.velocity, -velocityForTravel(180.0, kDecelerationRateNormal));
}

TEST(ScrollPhysicsTest, ClampsBetweenZeroAndTheContentThatDoesNotFit) {
    EXPECT_DOUBLE_EQ(maximumScrollOffset(kContentLength, kViewportLength), kMaximumOffset);
    EXPECT_DOUBLE_EQ(maximumScrollOffset(100.0, kViewportLength), 0.0);
    EXPECT_DOUBLE_EQ(clampScrollOffset(-30.0, kContentLength, kViewportLength), 0.0);
    EXPECT_DOUBLE_EQ(clampScrollOffset(400.0, kContentLength, kViewportLength), kMaximumOffset);
    EXPECT_DOUBLE_EQ(clampScrollOffset(200.0, kContentLength, kViewportLength), 200.0);
}

TEST(ScrollPhysicsTest, DecaysVelocityByTheRateRaisedToTheFrameTime) {
    const ScrollAxisState normal =
        decelerateAxis(ScrollAxisState{.offset = 0.0, .velocity = 1.0}, kFrameMilliseconds60Hz, kDecelerationRateNormal,
                       kUnboundedContent, kViewportLength);
    const ScrollAxisState fast = decelerateAxis(ScrollAxisState{.offset = 0.0, .velocity = 1.0}, kFrameMilliseconds60Hz,
                                                kDecelerationRateFast, kUnboundedContent, kViewportLength);

    EXPECT_NEAR(normal.velocity, kNormalDecayPerFrame60Hz, kDecayTolerance);
    EXPECT_NEAR(fast.velocity, kFastDecayPerFrame60Hz, kDecayTolerance);
}

TEST(ScrollPhysicsTest, TravelsExactlyTheDistanceAWheelNotchAsksFor) {
    const double travelled = settledOffset(velocityForTravel(kWheelNotchDistance, kDecelerationRateNormal),
                                           kFrameMilliseconds60Hz, kDecelerationRateNormal);

    EXPECT_NEAR(travelled, kWheelNotchDistance, 1e-9);
}

TEST(ScrollPhysicsTest, TravelsTheSameDistanceAtOneHundredAndTwentyHertzAsAtSixty) {
    const double velocity = velocityForTravel(3.0 * kWheelNotchDistance, kDecelerationRateNormal);

    EXPECT_NEAR(settledOffset(velocity, kFrameMilliseconds120Hz, kDecelerationRateNormal),
                settledOffset(velocity, kFrameMilliseconds60Hz, kDecelerationRateNormal), 1e-9);
}

TEST(ScrollPhysicsTest, AFasterRateCoversLessGroundForTheSameVelocity) {
    EXPECT_LT(settledOffset(1.0, kFrameMilliseconds60Hz, kDecelerationRateFast),
              settledOffset(1.0, kFrameMilliseconds60Hz, kDecelerationRateNormal));
}

TEST(ScrollPhysicsTest, FoldsTheLastHalfPointIntoTheStepThatStops) {
    const ScrollAxisState stopping =
        decelerateAxis(ScrollAxisState{.offset = 0.0, .velocity = velocityForTravel(0.4, kDecelerationRateNormal)},
                       kFrameMilliseconds60Hz, kDecelerationRateNormal, kUnboundedContent, kViewportLength);
    const ScrollAxisState gliding =
        decelerateAxis(ScrollAxisState{.offset = 0.0, .velocity = velocityForTravel(0.6, kDecelerationRateNormal)},
                       kFrameMilliseconds60Hz, kDecelerationRateNormal, kUnboundedContent, kViewportLength);

    EXPECT_EQ(stopping.velocity, 0.0);
    EXPECT_NEAR(stopping.offset, 0.4, 1e-12);
    EXPECT_GT(gliding.velocity, 0.0);
    EXPECT_LT(gliding.offset, 0.6);
}

TEST(ScrollPhysicsTest, ReachingTheEndOfTheContentStopsMomentumDead) {
    const ScrollAxisState advanced =
        decelerateAxis(ScrollAxisState{.offset = 318.0, .velocity = velocityForTravel(200.0, kDecelerationRateNormal)},
                       kFrameMilliseconds60Hz, kDecelerationRateNormal, kContentLength, kViewportLength);

    EXPECT_DOUBLE_EQ(advanced.offset, kMaximumOffset);
    EXPECT_EQ(advanced.velocity, 0.0);
}

TEST(ScrollPhysicsTest, ConfinesDecelerationRatesTheCurveIsNotDefinedAt) {
    // React Native accepts 0, meaning "stop the moment the finger lifts", and 1, meaning "never stop". The
    // momentum integral is defined at neither, so both resolve into the range where it is: a rate of 0 glides for
    // under two points and a rate of 1 for thousands, and both come to a stop rather than dividing by zero.
    EXPECT_LT(settledOffset(1.0, kFrameMilliseconds60Hz, 0.0), 2.0);
    EXPECT_GT(settledOffset(1.0, kFrameMilliseconds60Hz, 1.0), 1000.0);
}

TEST(ScrollPhysicsTest, ADragMovesOneToOneAndReportsTheVelocityItImplies) {
    const ScrollAxisState dragged = dragAxis(ScrollAxisState{}, 25.0, 10.0, kContentLength, kViewportLength);

    EXPECT_DOUBLE_EQ(dragged.offset, 25.0);
    EXPECT_DOUBLE_EQ(dragged.velocity, 2.5);
}

TEST(ScrollPhysicsTest, ADragThatHitsTheEndReportsOnlyTheDistanceItCovered) {
    const ScrollAxisState dragged =
        dragAxis(ScrollAxisState{.offset = 310.0, .velocity = 0.0}, 40.0, 10.0, kContentLength, kViewportLength);

    EXPECT_DOUBLE_EQ(dragged.offset, kMaximumOffset);
    EXPECT_DOUBLE_EQ(dragged.velocity, 1.0);
}

TEST(ScrollPhysicsTest, ADragInsideAZeroLengthFrameImpliesNoVelocity) {
    const ScrollAxisState dragged = dragAxis(ScrollAxisState{}, 25.0, 0.0, kContentLength, kViewportLength);

    EXPECT_DOUBLE_EQ(dragged.offset, 25.0);
    EXPECT_DOUBLE_EQ(dragged.velocity, 0.0);
}

TEST(ScrollQueueTest, SumsConsecutiveDeltasOnOneAxis) {
    InputQueue queue;

    queue.push(makeScroll(InputEventKind::PointerScrollContinuous, ScrollAxisKind::Vertical, 4.5));
    queue.push(makeScroll(InputEventKind::PointerScrollContinuous, ScrollAxisKind::Vertical, 5.5));

    const std::vector<InputEvent> drained = queue.drain();

    ASSERT_EQ(drained.size(), 1U);
    EXPECT_DOUBLE_EQ(drained[0].scrollAmount, 10.0);
}

TEST(ScrollQueueTest, KeepsTheTwoAxesApart) {
    InputQueue queue;

    queue.push(makeScroll(InputEventKind::PointerScrollContinuous, ScrollAxisKind::Vertical, 4.0));
    queue.push(makeScroll(InputEventKind::PointerScrollContinuous, ScrollAxisKind::Horizontal, 7.0));

    const std::vector<InputEvent> drained = queue.drain();

    ASSERT_EQ(drained.size(), 2U);
    EXPECT_EQ(drained[0].scrollAxis, ScrollAxisKind::Vertical);
    EXPECT_EQ(drained[1].scrollAxis, ScrollAxisKind::Horizontal);
}

TEST(ScrollQueueTest, DropsTheContinuousDeltaThatDuplicatesAWheelNotch) {
    InputQueue queue;

    // The order one wl_pointer frame carrying a wheel notch arrives in: the notch, then the same notch again as an
    // axis value in units the compositor chose.
    queue.push(makeScroll(InputEventKind::PointerScrollDiscrete, ScrollAxisKind::Vertical, 1.0));
    queue.push(makeScroll(InputEventKind::PointerScrollContinuous, ScrollAxisKind::Vertical, 15.0));
    queue.push(makeScroll(InputEventKind::PointerScrollDiscrete, ScrollAxisKind::Vertical, 1.0));
    queue.push(makeScroll(InputEventKind::PointerScrollContinuous, ScrollAxisKind::Vertical, 15.0));

    const std::vector<InputEvent> drained = queue.drain();

    ASSERT_EQ(drained.size(), 1U);
    EXPECT_EQ(drained[0].kind, InputEventKind::PointerScrollDiscrete);
    EXPECT_DOUBLE_EQ(drained[0].scrollAmount, 2.0);
}

TEST(ScrollQueueTest, ANotchBehindASmoothDeltaIsItsOwnEvent) {
    InputQueue queue;

    queue.push(makeScroll(InputEventKind::PointerScrollContinuous, ScrollAxisKind::Vertical, 12.0));
    queue.push(makeScroll(InputEventKind::PointerScrollDiscrete, ScrollAxisKind::Vertical, 1.0));

    const std::vector<InputEvent> drained = queue.drain();

    ASSERT_EQ(drained.size(), 2U);
    EXPECT_EQ(drained[0].kind, InputEventKind::PointerScrollContinuous);
    EXPECT_EQ(drained[1].kind, InputEventKind::PointerScrollDiscrete);
}

TEST(ScrollQueueTest, KeepsAStopBehindTheDeltasItEnds) {
    InputQueue queue;

    queue.push(makeScroll(InputEventKind::PointerScrollContinuous, ScrollAxisKind::Vertical, 8.0));
    queue.push(makeScroll(InputEventKind::PointerScrollStop, ScrollAxisKind::Vertical, 0.0));
    queue.push(makeScroll(InputEventKind::PointerScrollStop, ScrollAxisKind::Vertical, 0.0));

    const std::vector<InputEvent> drained = queue.drain();

    ASSERT_EQ(drained.size(), 3U);
    EXPECT_EQ(drained[2].kind, InputEventKind::PointerScrollStop);
}

TEST(ScrollQueueTest, AMotionBetweenTwoDeltasSplitsThem) {
    InputQueue queue;

    queue.push(makeScroll(InputEventKind::PointerScrollContinuous, ScrollAxisKind::Vertical, 8.0));
    queue.push(InputEvent{.kind = InputEventKind::PointerMotion, .surfacePoint = Point{.x = 10, .y = 10}});
    queue.push(makeScroll(InputEventKind::PointerScrollContinuous, ScrollAxisKind::Vertical, 8.0));

    EXPECT_EQ(queue.drain().size(), 3U);
}

TEST(ScrollRoutingTest, IdentifiesTheEventsTheScrollPipelineOwns) {
    EXPECT_TRUE(isScrollEvent(makeScroll(InputEventKind::PointerScrollContinuous, ScrollAxisKind::Vertical, 1.0)));
    EXPECT_TRUE(isScrollEvent(makeScroll(InputEventKind::PointerScrollDiscrete, ScrollAxisKind::Vertical, 1.0)));
    EXPECT_TRUE(isScrollEvent(makeScroll(InputEventKind::PointerScrollStop, ScrollAxisKind::Vertical, 0.0)));
    EXPECT_FALSE(isScrollEvent(InputEvent{.kind = InputEventKind::PointerMotion}));
}

TEST(ScrollRoutingTest, TheMousePointerRouterIgnoresScrollEvents) {
    PointerRouter router;
    const std::vector<InputEventKind> kinds{InputEventKind::PointerScrollContinuous,
                                            InputEventKind::PointerScrollDiscrete, InputEventKind::PointerScrollStop};

    for (InputEventKind kind : kinds) {
        const std::vector<PointerDispatch> dispatches =
            router.route(makeScroll(kind, ScrollAxisKind::Vertical, 1.0), 4, Point{});

        EXPECT_TRUE(dispatches.empty());
    }
}

#pragma mark - the controller wiring (#45)

/**
 * The controller-level rig: a real UIManager whose registry holds a real ShadowTree committed with one
 * ScrollView (100x100) over a 100x300 page, so wheel input hit-tests, integrates and publishes exactly the way
 * the window path runs it. The event dispatcher is the empty one the mounting tests use: emissions drop on the
 * floor, which makes `hasDispatchedScrollEvent` and `isScrollActive` the two observables, together with
 * whatever the frames settle into.
 */
class ScrollControllerTest : public ::testing::Test {
protected:
    ScrollControllerTest() {
        uiManager_ =
            std::make_shared<UIManager>([](std::function<void(facebook::jsi::Runtime&)>&&) {}, contextContainer_);

        auto shadowTree = std::make_unique<ShadowTree>(kSurfaceId, LayoutConstraints{}, LayoutContext{},
                                                       shadowTreeDelegate_, *contextContainer_);

        shadowTree_ = shadowTree.get();
        uiManager_->getShadowTreeRegistry().add(std::move(shadowTree));
    }

    void commitScrollView(folly::dynamic scrollViewProps) {
        scrollViewProps["width"] = 100;
        scrollViewProps["height"] = 100;
        const ShadowTreeCommitOptions commitOptions{.enableStateReconciliation = false, .mountSynchronously = true};

        shadowTree_->commit(
            [this, &scrollViewProps](const RootShadowNode& oldRootShadowNode) {
                return std::static_pointer_cast<RootShadowNode>(oldRootShadowNode.ShadowNode::clone(ShadowNodeFragment{
                    .props = ShadowNodeFragment::propsPlaceholder(),
                    .children =
                        std::make_shared<const ChildList>(ChildList{makeScrollView(20, std::move(scrollViewProps))})}));
            },
            commitOptions);
    }

    ScrollController makeController() { return ScrollController(uiManager_, kSurfaceId); }

    static InputEvent wheel(double notches, double x = 50.0, double y = 50.0) {
        return InputEvent{.kind = InputEventKind::PointerScrollDiscrete,
                          .surfacePoint = Point{.x = static_cast<float>(x), .y = static_cast<float>(y)},
                          .scrollAxis = ScrollAxisKind::Vertical,
                          .scrollAmount = notches};
    }

    static InputEvent drag(double amount, double x = 50.0, double y = 50.0) {
        return InputEvent{.kind = InputEventKind::PointerScrollContinuous,
                          .surfacePoint = Point{.x = static_cast<float>(x), .y = static_cast<float>(y)},
                          .scrollAxis = ScrollAxisKind::Vertical,
                          .scrollAmount = amount};
    }

    static InputEvent scrollStop() { return InputEvent{.kind = InputEventKind::PointerScrollStop}; }

    ShadowTree* shadowTree_;

private:
    std::shared_ptr<const ShadowNode> makeScrollView(Tag tag, folly::dynamic props) {
        const ShadowNodeFamily::Shared family =
            scrollViewDescriptor_.createFamily({.tag = tag, .surfaceId = kSurfaceId, .instanceHandle = nullptr});

        const PropsParserContext parserContext{kSurfaceId, *contextContainer_};
        RawProps rawProps{folly::dynamic(std::move(props))};

        const facebook::react::Props::Shared scrollViewProps = scrollViewDescriptor_.cloneProps(
            parserContext, ScrollViewShadowNode::defaultSharedProps(), std::move(rawProps));

        std::vector<std::shared_ptr<const ShadowNode>> children;
        children.push_back(makeChild(21, 100, 300));

        const ShadowNodeFragment scrollViewFragment{
            .props = scrollViewProps,
            .children = std::make_shared<const ChildList>(std::move(children)),
            .state = scrollViewDescriptor_.createInitialState(scrollViewProps, family)};

        return scrollViewDescriptor_.createShadowNode(scrollViewFragment, family);
    }

    std::shared_ptr<const ShadowNode> makeChild(Tag tag, double width, double height) {
        const ShadowNodeFamily::Shared family =
            viewDescriptor_.createFamily({.tag = tag, .surfaceId = kSurfaceId, .instanceHandle = nullptr});

        const PropsParserContext parserContext{kSurfaceId, *contextContainer_};
        RawProps rawProps{folly::dynamic::object("width", width)("height", height)};

        const facebook::react::Props::Shared childProps =
            viewDescriptor_.cloneProps(parserContext, ViewShadowNode::defaultSharedProps(), std::move(rawProps));

        return viewDescriptor_.createShadowNode(
            ShadowNodeFragment{.props = childProps, .children = std::make_shared<const ChildList>()}, family);
    }

    PassThroughShadowTreeDelegate shadowTreeDelegate_;
    std::shared_ptr<const ContextContainer> contextContainer_{std::make_shared<ContextContainer>()};
    ViewComponentDescriptor viewDescriptor_ = makeViewComponentDescriptor(contextContainer_);
    ScrollViewComponentDescriptor scrollViewDescriptor_{ComponentDescriptorParameters{
        .eventDispatcher = EventDispatcher::Shared{}, .contextContainer = contextContainer_, .flavor = nullptr}};

protected:
    void TearDown() override {
        // The registry asserts on destruction while it still holds trees.
        removedTree_ = uiManager_->getShadowTreeRegistry().remove(kSurfaceId);
    }

    std::unique_ptr<ShadowTree> removedTree_;
    std::shared_ptr<UIManager> uiManager_;
};

TEST_F(ScrollControllerTest, AWheelFlingStaysActiveThroughMomentumAndSettles) {
    commitScrollView(folly::dynamic::object());
    ScrollController controller = makeController();

    controller.dispatch({wheel(3)});

    controller.advance(kFrameMilliseconds60Hz);

    EXPECT_TRUE(controller.hasDispatchedScrollEvent());
    EXPECT_TRUE(controller.isScrollActive());

    bool settled = false;

    for (int frame = 0; frame < 400; frame++) {
        if (!controller.advance(100.0)) {
            settled = true;

            break;
        }
    }

    EXPECT_TRUE(settled);
    EXPECT_FALSE(controller.isScrollActive());

    // One idle frame: the flag describes the last advance, and the frame after settling moved nothing.
    controller.advance(kFrameMilliseconds60Hz);

    EXPECT_FALSE(controller.hasDispatchedScrollEvent());
}

TEST_F(ScrollControllerTest, AContinuousDragIsNotMomentumUntilTheStopReleasesIt) {
    commitScrollView(folly::dynamic::object());
    ScrollController controller = makeController();

    controller.dispatch({drag(5)});

    EXPECT_TRUE(controller.isScrollActive());

    controller.advance(kFrameMilliseconds60Hz);

    EXPECT_TRUE(controller.hasDispatchedScrollEvent());

    controller.dispatch({scrollStop()});
    controller.advance(kFrameMilliseconds60Hz);

    EXPECT_FALSE(controller.isScrollActive());
}

TEST_F(ScrollControllerTest, TheThrottleGatesEmissionThroughConsecutiveMovingFrames) {
    commitScrollView(folly::dynamic::object("scrollEventThrottle", 16));
    ScrollController controller = makeController();

    controller.dispatch({drag(5)});

    controller.advance(8.0);

    EXPECT_FALSE(controller.hasDispatchedScrollEvent());
    EXPECT_TRUE(controller.isScrollActive());

    controller.advance(8.0);

    EXPECT_TRUE(controller.hasDispatchedScrollEvent());
}

TEST_F(ScrollControllerTest, AScrollToCommandMovesOnceAndAScrollToWhereYouAreEmitsNothing) {
    commitScrollView(folly::dynamic::object());
    ScrollController controller = makeController();

    controller.dispatchCommands(
        {SceneCommand{.tag = 20, .name = "scrollTo", .args = folly::dynamic::array(0, 150, false)}});

    controller.advance(kFrameMilliseconds60Hz);

    EXPECT_TRUE(controller.hasDispatchedScrollEvent());

    controller.advance(kFrameMilliseconds60Hz);

    EXPECT_FALSE(controller.isScrollActive());
    EXPECT_FALSE(controller.hasDispatchedScrollEvent());

    controller.dispatchCommands(
        {SceneCommand{.tag = 20, .name = "scrollTo", .args = folly::dynamic::array(0, 150, false)}});

    controller.advance(kFrameMilliseconds60Hz);

    EXPECT_FALSE(controller.isScrollActive());
    EXPECT_FALSE(controller.hasDispatchedScrollEvent());
}

TEST_F(ScrollControllerTest, ProbeHitChain) {
    commitScrollView(folly::dynamic::object());

    std::shared_ptr<const ShadowNode> root;
    uiManager_->getShadowTreeRegistry().visit(
        kSurfaceId, [&root](const ShadowTree& tree) { root = tree.getCurrentRevision().rootShadowNode; });

    GTEST_LOG_(INFO) << "PROBE root=" << (root != nullptr);

    const std::shared_ptr<const ShadowNode> hit = uiManager_->findNodeAtPoint(root, Point{.x = 50, .y = 50});

    GTEST_LOG_(INFO) << "PROBE hit=" << (hit != nullptr) << " tag=" << (hit ? hit->getTag() : 0);

    if (hit != nullptr) {
        const auto ancestors = hit->getFamily().getAncestors(*root);

        GTEST_LOG_(INFO) << "PROBE ancestors=" << ancestors.size();

        for (const auto& entry : ancestors) {
            const ShadowNode& ancestorNode = entry.first;
            const auto* scrollView = dynamic_cast<const ScrollViewShadowNode*>(&ancestorNode);

            GTEST_LOG_(INFO) << "PROBE ancestor tag=" << ancestorNode.getTag()
                             << " isScrollView=" << (scrollView != nullptr);
        }
    }

    ScrollController controller = makeController();

    controller.dispatch({wheel(3)});

    GTEST_LOG_(INFO) << "PROBE after dispatch: active=" << controller.isScrollActive();

    controller.advance(kFrameMilliseconds60Hz);

    GTEST_LOG_(INFO) << "PROBE dispatched=" << controller.hasDispatchedScrollEvent()
                     << " active=" << controller.isScrollActive();

    controller.dispatch({wheel(3)});
    controller.advance(kFrameMilliseconds60Hz);

    GTEST_LOG_(INFO) << "PROBE second: dispatched=" << controller.hasDispatchedScrollEvent()
                     << " active=" << controller.isScrollActive();

    controller.dispatchCommands(
        {SceneCommand{.tag = 20, .name = "scrollTo", .args = folly::dynamic::array(0, 150, false)}});

    GTEST_LOG_(INFO) << "PROBE after scrollTo: active=" << controller.isScrollActive();

    controller.advance(kFrameMilliseconds60Hz);

    GTEST_LOG_(INFO) << "PROBE after scrollTo advance: dispatched=" << controller.hasDispatchedScrollEvent()
                     << " active=" << controller.isScrollActive();

    std::shared_ptr<const ShadowNode> scrollViewNode;
    uiManager_->getShadowTreeRegistry().visit(kSurfaceId, [&root, &scrollViewNode](const ShadowTree& tree) {
        std::shared_ptr<const ShadowNode> rootNode = tree.getCurrentRevision().rootShadowNode;

        for (const std::shared_ptr<const ShadowNode>& child : rootNode->getChildren()) {
            if (child->getTag() == 20) {
                scrollViewNode = child;
            }
        }
    });

    if (scrollViewNode == nullptr) {
        GTEST_LOG_(INFO) << "PROBE scrollview node not found";

        SUCCEED();

        return;
    }

    GTEST_LOG_(INFO) << "PROBE family surface=" << scrollViewNode->getFamily().getSurfaceId();

    const std::shared_ptr<const ShadowNode> clone = uiManager_->getNewestCloneOfShadowNode(*scrollViewNode);

    GTEST_LOG_(INFO) << "PROBE clone=" << (clone != nullptr);

    const auto* scrollView = dynamic_cast<const ScrollViewShadowNode*>(scrollViewNode.get());

    if (scrollView != nullptr) {
        GTEST_LOG_(INFO) << "PROBE scrollEnabled=" << scrollView->getConcreteProps().scrollEnabled
                         << " contentH=" << scrollView->getStateData().getContentSize().height
                         << " emitter=" << (scrollView->getEventEmitter() != nullptr);
    }

    SUCCEED();
}

} // namespace
