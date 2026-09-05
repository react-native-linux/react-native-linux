#include "InputPipeline.h"
#include "ScrollController.h"
#include "ScrollEventCadence.h"
#include "ScrollPhysics.h"
#include "ShadowTreeTestSupport.h"
#include "TextInputComponent.h"

#include <LinuxMountingManager.h>
#include <cstddef>
#include <gtest/gtest.h>
#include <react/renderer/components/scrollview/ScrollViewComponentDescriptor.h>
#include <react/renderer/components/scrollview/ScrollViewShadowNode.h>
#include <react/renderer/core/LayoutConstraints.h>
#include <react/renderer/core/LayoutContext.h>
#include <react/renderer/graphics/Point.h>
#include <react/renderer/mounting/ShadowTree.h>
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
using react_native_linux::hasSnapPoints;
using react_native_linux::isScrollEvent;
using react_native_linux::kDecelerationRateFast;
using react_native_linux::kDecelerationRateNormal;
using react_native_linux::kWheelNotchDistance;
using react_native_linux::makeConfiguredShadowNode;
using react_native_linux::makeTaskDroppingUIManager;
using react_native_linux::maintainedScrollOffset;
using react_native_linux::MaintainVisibleContentPosition;
using react_native_linux::maximumScrollOffset;
using react_native_linux::PassThroughShadowTreeDelegate;
using react_native_linux::PointerDispatch;
using react_native_linux::PointerRouter;
using react_native_linux::SceneCommand;
using react_native_linux::ScrollAxisKind;
using react_native_linux::ScrollAxisState;
using react_native_linux::ScrollCadenceEvents;
using react_native_linux::ScrollChildFrame;
using react_native_linux::ScrollCadenceFrame;
using react_native_linux::ScrollController;
using react_native_linux::ScrollDestination;
using react_native_linux::ScrollEventCadence;
using react_native_linux::ScrollSnapAlignment;
using react_native_linux::ScrollSnapConfiguration;
using react_native_linux::scrollToDestination;
using react_native_linux::settleTargetOffset;
using react_native_linux::TextInputComponentDescriptor;
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

// Issue #46. A ScrollView's content can shrink under an offset that was valid a frame ago — a list emptied, a
// paragraph re-measured shorter, an image that turned out smaller than its placeholder — and the offset has to
// follow it down in the same frame rather than sit past the end of the content until the next gesture. The rest
// state is the case that matters: nothing is moving, so only the per-frame re-clamp can do it.

TEST(ScrollPhysicsTest, AnOffsetAtRestFollowsContentThatShrankBelowIt) {
    // 320 was the end of 470 points of content; the content is now 300, so the end is 150.
    const ScrollAxisState resting{.offset = kMaximumOffset, .velocity = 0.0};
    const ScrollAxisState followed = decelerateAxis(resting, kFrameMilliseconds60Hz, kDecelerationRateNormal,
                                                    300.0, kViewportLength);

    EXPECT_DOUBLE_EQ(followed.offset, 150.0);
    EXPECT_DOUBLE_EQ(followed.velocity, 0.0);
}

TEST(ScrollPhysicsTest, AnOffsetAtRestStaysPutWhenTheContentGrows) {
    const ScrollAxisState resting{.offset = 120.0, .velocity = 0.0};
    const ScrollAxisState unchanged = decelerateAxis(resting, kFrameMilliseconds60Hz, kDecelerationRateNormal,
                                                     kContentLength * 2, kViewportLength);

    EXPECT_DOUBLE_EQ(unchanged.offset, 120.0);
}

TEST(ScrollPhysicsTest, ContentThatNoLongerFillsTheViewportSendsTheOffsetHome) {
    const ScrollAxisState resting{.offset = 40.0, .velocity = 0.0};
    const ScrollAxisState home = decelerateAxis(resting, kFrameMilliseconds60Hz, kDecelerationRateNormal, 100.0,
                                                kViewportLength);

    EXPECT_DOUBLE_EQ(home.offset, 0.0);
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

#pragma mark - the settle target (#239)

// The table `settleTargetOffset` is graded against. Every row is one flick: where it started, how far its
// velocity would carry it if nothing snapped, and where it has to come to rest instead.
//
// `travel` rather than a velocity, because a velocity is not a distance anybody can read: `velocityForTravel`
// turns the number in the row into the velocity whose curve covers exactly it, which is the same inversion a
// wheel notch uses.

struct SettleCase {
    const char* what;
    double offset;
    double travel;
    double contentLength;
    double viewportLength;
    ScrollSnapConfiguration snapping;
    double expected;
};

double settleTargetFor(const SettleCase& settleCase) {
    const ScrollAxisState axis{.offset = settleCase.offset,
                               .velocity = velocityForTravel(settleCase.travel, kDecelerationRateNormal)};

    return settleTargetOffset(axis, kDecelerationRateNormal, settleCase.contentLength, settleCase.viewportLength,
                              settleCase.snapping);
}

// A five-page carousel, one page per viewport. The fractional pair is core#48393's case: a 150 point viewport
// under wp_fractional_scale_v1 is 149.5 points wide, and the snap points are fractional with it.
constexpr double kPagedViewport = 150.0;
constexpr double kPagedContent = 750.0;
constexpr double kFractionalViewport = 149.5;
constexpr double kFractionalContent = 747.5;

// A 411 physical-pixel viewport at scale 1.25 is 328.8 points, which is neither an integer nor a value any
// rounding of the viewport reproduces.
constexpr double kScaledViewport = 411.0 / 1.25;
constexpr double kScaledContent = kScaledViewport * 5.0;

ScrollSnapConfiguration paging() { return ScrollSnapConfiguration{.isPagingEnabled = true}; }

ScrollSnapConfiguration interval(double length, ScrollSnapAlignment alignment) {
    return ScrollSnapConfiguration{.interval = length, .alignment = alignment};
}

ScrollSnapConfiguration offsets(std::vector<double> points) {
    return ScrollSnapConfiguration{.offsets = std::move(points)};
}

TEST(SettleTargetTest, ProjectsEveryFlickOntoTheSnapPointItsPropsDescribe) {
    const std::vector<SettleCase> cases{
        {.what = "no snap point at all leaves the landing point where momentum put it",
         .offset = 0.0,
         .travel = 120.0,
         .contentLength = kContentLength,
         .viewportLength = kViewportLength,
         .snapping = ScrollSnapConfiguration{},
         .expected = 120.0},
        {.what = "a landing point past the end is clamped before anything is projected onto it",
         .offset = 0.0,
         .travel = 5000.0,
         .contentLength = kContentLength,
         .viewportLength = kViewportLength,
         .snapping = ScrollSnapConfiguration{},
         .expected = kMaximumOffset},
        {.what = "paging lands on the page nearest where momentum stopped",
         .offset = 0.0,
         .travel = 160.0,
         .contentLength = kPagedContent,
         .viewportLength = kPagedViewport,
         .snapping = paging(),
         .expected = 150.0},
        {.what = "less than half a page forward stays on the page it started on",
         .offset = 0.0,
         .travel = 70.0,
         .contentLength = kPagedContent,
         .viewportLength = kPagedViewport,
         .snapping = paging(),
         .expected = 0.0},
        {.what = "more than half a page forward takes the next one",
         .offset = 0.0,
         .travel = 80.0,
         .contentLength = kPagedContent,
         .viewportLength = kPagedViewport,
         .snapping = paging(),
         .expected = 150.0},
        {.what = "a backwards flick snaps the same way",
         .offset = 300.0,
         .travel = -160.0,
         .contentLength = kPagedContent,
         .viewportLength = kPagedViewport,
         .snapping = paging(),
         .expected = 150.0},
        {.what = "core#48393: a fractional viewport has fractional pages and still snaps",
         .offset = 0.0,
         .travel = 160.0,
         .contentLength = kFractionalContent,
         .viewportLength = kFractionalViewport,
         .snapping = paging(),
         .expected = kFractionalViewport},
        {.what = "core#48393 at a 1.25 scale: 411 physical pixels is 328.8 points and page two is at 328.8",
         .offset = 0.0,
         .travel = 400.0,
         .contentLength = kScaledContent,
         .viewportLength = kScaledViewport,
         .snapping = paging(),
         .expected = kScaledViewport},
        {.what = "an interval snaps to its multiples",
         .offset = 0.0,
         .travel = 260.0,
         .contentLength = kContentLength,
         .viewportLength = kViewportLength,
         .snapping = interval(100.0, ScrollSnapAlignment::Start),
         .expected = 300.0},
        {.what = "a centred interval leaves half the leftover viewport on each side of the item",
         .offset = 0.0,
         .travel = 260.0,
         .contentLength = kContentLength,
         .viewportLength = kViewportLength,
         .snapping = interval(100.0, ScrollSnapAlignment::Center),
         .expected = 275.0},
        {.what = "an end-aligned interval puts the item's trailing edge on the viewport's",
         .offset = 0.0,
         .travel = 260.0,
         .contentLength = kContentLength,
         .viewportLength = kViewportLength,
         .snapping = interval(100.0, ScrollSnapAlignment::End),
         .expected = 250.0},
        {.what = "an item longer than the viewport centres by a negative shift",
         .offset = 0.0,
         .travel = 160.0,
         .contentLength = kContentLength,
         .viewportLength = kViewportLength,
         .snapping = interval(200.0, ScrollSnapAlignment::Center),
         .expected = 225.0},
        {.what = "the end of the content is a snap point of its own, so the last partial page is reachable",
         .offset = 0.0,
         .travel = 315.0,
         .contentLength = kContentLength,
         .viewportLength = kViewportLength,
         .snapping = interval(100.0, ScrollSnapAlignment::Start),
         .expected = kMaximumOffset},
        {.what = "snapToEnd false lets the content settle past the last interval instead",
         .offset = 0.0,
         .travel = 315.0,
         .contentLength = kContentLength,
         .viewportLength = kViewportLength,
         .snapping = ScrollSnapConfiguration{.interval = 100.0, .snapToEnd = false},
         .expected = 315.0},
        {.what = "snapToStart false lets it settle before the first one",
         .offset = 0.0,
         .travel = 30.0,
         .contentLength = kContentLength,
         .viewportLength = kViewportLength,
         .snapping = ScrollSnapConfiguration{.interval = 100.0,
                                             .alignment = ScrollSnapAlignment::Center,
                                             .snapToStart = false},
         .expected = 30.0},
        {.what = "an interval with no multiple inside the scrollable range snaps to nothing",
         .offset = 0.0,
         .travel = 0.0,
         .contentLength = kViewportLength,
         .viewportLength = kViewportLength,
         .snapping = interval(100.0, ScrollSnapAlignment::End),
         .expected = 0.0},
        {.what = "an explicit offset list snaps to its nearest entry",
         .offset = 0.0,
         .travel = 200.0,
         .contentLength = kContentLength,
         .viewportLength = kViewportLength,
         .snapping = offsets({0.0, 90.0, 210.0, kMaximumOffset}),
         .expected = 210.0},
        {.what = "offsets outside the scrollable range are not snap points",
         .offset = 0.0,
         .travel = 200.0,
         .contentLength = kContentLength,
         .viewportLength = kViewportLength,
         .snapping = offsets({-50.0, 90.0, 900.0}),
         .expected = 90.0},
        {.what = "the start of the content is a snap point beside an offset list with no entry there",
         .offset = 0.0,
         .travel = 30.0,
         .contentLength = kContentLength,
         .viewportLength = kViewportLength,
         .snapping = offsets({90.0, 210.0}),
         .expected = 0.0},
        {.what = "snapToStart false frees the range between the start and the first offset",
         .offset = 0.0,
         .travel = 30.0,
         .contentLength = kContentLength,
         .viewportLength = kViewportLength,
         .snapping = ScrollSnapConfiguration{.offsets = {90.0, 210.0}, .snapToStart = false},
         .expected = 30.0},
        {.what = "snapToEnd false frees the range between the last offset and the end",
         .offset = 0.0,
         .travel = 300.0,
         .contentLength = kContentLength,
         .viewportLength = kViewportLength,
         .snapping = ScrollSnapConfiguration{.offsets = {0.0, 90.0, 210.0}, .snapToEnd = false},
         .expected = 300.0},
        {.what = "snapToStart false still snaps a landing point that is not before the first offset",
         .offset = 0.0,
         .travel = 200.0,
         .contentLength = kContentLength,
         .viewportLength = kViewportLength,
         .snapping = ScrollSnapConfiguration{.offsets = {90.0, 210.0}, .snapToStart = false},
         .expected = 210.0},
        {.what = "snapToEnd false still snaps a landing point that is not past the last offset",
         .offset = 0.0,
         .travel = 200.0,
         .contentLength = kContentLength,
         .viewportLength = kViewportLength,
         .snapping = ScrollSnapConfiguration{.offsets = {0.0, 90.0, 210.0}, .snapToEnd = false},
         .expected = 210.0},
        {.what = "an offset list wins over an interval, which is the order RCTScrollView resolves them in",
         .offset = 0.0,
         .travel = 200.0,
         .contentLength = kContentLength,
         .viewportLength = kViewportLength,
         .snapping = ScrollSnapConfiguration{.interval = 100.0, .offsets = {0.0, 90.0, 210.0}},
         .expected = 210.0},
        {.what = "disableIntervalMomentum takes the point ahead of the release, not the one momentum would reach",
         .offset = 20.0,
         .travel = 400.0,
         .contentLength = kPagedContent,
         .viewportLength = kPagedViewport,
         .snapping = ScrollSnapConfiguration{.isPagingEnabled = true, .isIntervalMomentumDisabled = true},
         .expected = 150.0},
        {.what = "a release exactly on a point with disableIntervalMomentum advances rather than travelling zero",
         .offset = 150.0,
         .travel = 400.0,
         .contentLength = kPagedContent,
         .viewportLength = kPagedViewport,
         .snapping = ScrollSnapConfiguration{.isPagingEnabled = true, .isIntervalMomentumDisabled = true},
         .expected = 300.0},
        {.what = "the same release backwards takes the point behind it",
         .offset = 150.0,
         .travel = -400.0,
         .contentLength = kPagedContent,
         .viewportLength = kPagedViewport,
         .snapping = ScrollSnapConfiguration{.isPagingEnabled = true, .isIntervalMomentumDisabled = true},
         .expected = 0.0},
        {.what = "a directional release with nothing ahead of it stays on the point it is already on",
         .offset = 600.0,
         .travel = 400.0,
         .contentLength = kPagedContent,
         .viewportLength = kPagedViewport,
         .snapping = ScrollSnapConfiguration{.isPagingEnabled = true, .isIntervalMomentumDisabled = true},
         .expected = 600.0},
        {.what = "a motionless release with disableIntervalMomentum has no direction and takes the nearest",
         .offset = 160.0,
         .travel = 0.0,
         .contentLength = kPagedContent,
         .viewportLength = kPagedViewport,
         .snapping = ScrollSnapConfiguration{.isPagingEnabled = true, .isIntervalMomentumDisabled = true},
         .expected = 150.0},
        {.what = "a motionless release settles onto the nearest snap point, which is what UIScrollView pages on",
         .offset = 80.0,
         .travel = 0.0,
         .contentLength = kPagedContent,
         .viewportLength = kPagedViewport,
         .snapping = paging(),
         .expected = 150.0},
        {.what = "a motionless release with nothing configured stays exactly where the finger left it",
         .offset = 80.0,
         .travel = 0.0,
         .contentLength = kPagedContent,
         .viewportLength = kPagedViewport,
         .snapping = ScrollSnapConfiguration{},
         .expected = 80.0},
        {.what = "a directional release takes the nearest point ahead of it however the offsets were listed",
         .offset = 50.0,
         .travel = 400.0,
         .contentLength = kContentLength,
         .viewportLength = kViewportLength,
         .snapping = ScrollSnapConfiguration{.offsets = {0.0, 300.0, 90.0}, .isIntervalMomentumDisabled = true},
         .expected = 90.0},
        {.what = "snapToInterval takes precedence over pagingEnabled, which the prop is documented as replacing",
         .offset = 0.0,
         .travel = 160.0,
         .contentLength = kContentLength,
         .viewportLength = kViewportLength,
         .snapping = ScrollSnapConfiguration{.interval = 100.0, .isPagingEnabled = true},
         .expected = 200.0},
        {.what = "the same flick with momentum left on carries three pages further",
         .offset = 20.0,
         .travel = 400.0,
         .contentLength = kPagedContent,
         .viewportLength = kPagedViewport,
         .snapping = paging(),
         .expected = 450.0},
        {.what = "content shorter than its viewport has one snap point, at the origin",
         .offset = 0.0,
         .travel = 120.0,
         .contentLength = 100.0,
         .viewportLength = kPagedViewport,
         .snapping = paging(),
         .expected = 0.0}};

    for (const SettleCase& settleCase : cases) {
        SCOPED_TRACE(settleCase.what);

        EXPECT_DOUBLE_EQ(settleTargetFor(settleCase), settleCase.expected);
    }
}

TEST(SettleTargetTest, KnowsWhichConfigurationsDescribeASnapPointAtAll) {
    EXPECT_FALSE(hasSnapPoints(ScrollSnapConfiguration{}));
    EXPECT_TRUE(hasSnapPoints(paging()));
    EXPECT_TRUE(hasSnapPoints(interval(100.0, ScrollSnapAlignment::Start)));
    EXPECT_TRUE(hasSnapPoints(offsets({90.0})));
}

#pragma mark - maintainVisibleContentPosition (#240)

// The table `maintainedScrollOffset` is graded against. Every row is one commit: the children before it, the
// children after it, where the content was, and where it has to be so that the child the user was looking at is
// painted in the same place.
//
// The list is a chat log: rows 100 points tall, laid out from zero, named by tag. A prepend is the same tags with
// new ones in front of them, which is the only thing that distinguishes it from a scroll — the indices all moved
// and the tags did not.

constexpr double kRowLength = 100.0;

// Room for every row of every case below, so a case that is about the clamp says so rather than meeting it by
// accident.
constexpr double kLogContent = 2000.0;
constexpr double kLogViewport = 150.0;

struct MaintainCase {
    const char* what;
    double offset;
    std::vector<ScrollChildFrame> previousChildren;
    std::vector<ScrollChildFrame> currentChildren;
    MaintainVisibleContentPosition maintaining;
    double expected;
    double contentLength{kLogContent};
    double viewportLength{kLogViewport};
};

std::vector<ScrollChildFrame> rows(const std::vector<int>& tags) {
    std::vector<ScrollChildFrame> children;

    for (size_t index = 0; index < tags.size(); ++index) {
        children.push_back(ScrollChildFrame{.tag = tags[index],
                                            .position = static_cast<double>(index) * kRowLength,
                                            .length = kRowLength});
    }

    return children;
}

// The log as the reader has it, and the same log with two messages prepended above everything in it.
std::vector<ScrollChildFrame> logRows() { return rows({11, 12, 13}); }

std::vector<ScrollChildFrame> prependedRows() { return rows({21, 22, 11, 12, 13}); }

TEST(MaintainVisibleContentPositionTest, HoldsTheVisibleChildStillAcrossEveryCommitThatMovedIt) {
    const std::vector<MaintainCase> cases{
        {.what = "two rows prepended above the anchor move the offset by exactly what the anchor moved",
         .offset = 100.0,
         .previousChildren = logRows(),
         .currentChildren = prependedRows(),
         .maintaining = MaintainVisibleContentPosition{},
         .expected = 300.0},
        {.what = "the anchor is the first row any part of which is visible, not the first fully visible one",
         .offset = 150.0,
         .previousChildren = logRows(),
         .currentChildren = prependedRows(),
         .maintaining = MaintainVisibleContentPosition{},
         .expected = 350.0},
        {.what = "minIndexForVisible skips the header the list keeps pinned at the top",
         .offset = 0.0,
         .previousChildren = logRows(),
         .currentChildren = rows({11, 21, 12, 13}),
         .maintaining = MaintainVisibleContentPosition{.minimumIndexForVisible = 1},
         .expected = 100.0},
        {.what = "a minIndexForVisible past the last child is no anchor at all",
         .offset = 100.0,
         .previousChildren = logRows(),
         .currentChildren = prependedRows(),
         .maintaining = MaintainVisibleContentPosition{.minimumIndexForVisible = 5},
         .expected = 100.0},
        {.what = "a negative minIndexForVisible reads as the first child rather than as an index",
         .offset = 0.0,
         .previousChildren = logRows(),
         .currentChildren = prependedRows(),
         .maintaining = MaintainVisibleContentPosition{.minimumIndexForVisible = -1},
         .expected = 200.0},
        {.what = "an offset past every child anchors on the last one",
         .offset = 500.0,
         .previousChildren = logRows(),
         .currentChildren = prependedRows(),
         .maintaining = MaintainVisibleContentPosition{},
         .expected = 700.0},
        {.what = "rows removed from the start move the offset back by what the anchor came up",
         .offset = 200.0,
         .previousChildren = logRows(),
         .currentChildren = rows({12, 13}),
         .maintaining = MaintainVisibleContentPosition{},
         .expected = 100.0},
        {.what = "an anchor the commit unmounted leaves the offset where it was",
         .offset = 100.0,
         .previousChildren = logRows(),
         .currentChildren = {ScrollChildFrame{.tag = 11, .position = 0.0, .length = kRowLength},
                             ScrollChildFrame{.tag = 13, .position = kRowLength, .length = kRowLength}},
         .maintaining = MaintainVisibleContentPosition{},
         .expected = 100.0},
        {.what = "an anchor that moved by less than half a point is an anchor that did not move",
         .offset = 100.0,
         .previousChildren = logRows(),
         .currentChildren = {ScrollChildFrame{.tag = 11, .position = 0.0, .length = kRowLength},
                             ScrollChildFrame{.tag = 12, .position = 100.4, .length = kRowLength},
                             ScrollChildFrame{.tag = 13, .position = 200.4, .length = kRowLength}},
         .maintaining = MaintainVisibleContentPosition{},
         .expected = 100.0},
        {.what = "a reader within autoscrollToTopThreshold of the top is taken to the new top",
         .offset = 40.0,
         .previousChildren = logRows(),
         .currentChildren = prependedRows(),
         .maintaining = MaintainVisibleContentPosition{.autoscrollToTopThreshold = 50.0},
         .expected = 0.0},
        {.what = "a reader past the threshold is pinned to the anchor instead",
         .offset = 60.0,
         .previousChildren = logRows(),
         .currentChildren = prependedRows(),
         .maintaining = MaintainVisibleContentPosition{.autoscrollToTopThreshold = 50.0},
         .expected = 260.0},
        {.what = "the threshold is not consulted by a commit that moved nothing",
         .offset = 40.0,
         .previousChildren = logRows(),
         .currentChildren = logRows(),
         .maintaining = MaintainVisibleContentPosition{.autoscrollToTopThreshold = 50.0},
         .expected = 40.0},
        {.what = "the first frame of a target has nothing to compare against and adjusts nothing",
         .offset = 100.0,
         .previousChildren = {},
         .currentChildren = prependedRows(),
         .maintaining = MaintainVisibleContentPosition{},
         .expected = 100.0},
        {.what = "the adjusted offset is clamped against the content the commit produced",
         .offset = 300.0,
         .previousChildren = logRows(),
         .currentChildren = prependedRows(),
         .maintaining = MaintainVisibleContentPosition{},
         .expected = 450.0,
         .contentLength = 600.0},
    };

    for (const MaintainCase& maintainCase : cases) {
        EXPECT_DOUBLE_EQ(maintainedScrollOffset(maintainCase.offset, maintainCase.previousChildren,
                                                maintainCase.currentChildren, maintainCase.maintaining,
                                                maintainCase.contentLength, maintainCase.viewportLength),
                         maintainCase.expected)
            << maintainCase.what;
    }
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
        uiManager_ = makeTaskDroppingUIManager(contextContainer_);
        shadowTree_ = addRegisteredShadowTree(*uiManager_, shadowTreeDelegate_, *contextContainer_, kSurfaceId);
    }

    void commitScrollView(folly::dynamic scrollViewProps, double contentHeight = 300) {
        scrollViewProps["width"] = 100;
        scrollViewProps["height"] = 100;
        const ShadowTreeCommitOptions commitOptions{.enableStateReconciliation = false, .mountSynchronously = true};

        shadowTree_->commit(
            [this, &scrollViewProps, contentHeight](const RootShadowNode& oldRootShadowNode) {
                return std::static_pointer_cast<RootShadowNode>(oldRootShadowNode.ShadowNode::clone(
                    ShadowNodeFragment{.props = ShadowNodeFragment::propsPlaceholder(),
                                       .children = std::make_shared<const ChildList>(
                                           ChildList{makeScrollView(20, std::move(scrollViewProps), contentHeight)})}));
            },
            commitOptions);
    }

    ScrollController makeController() { return ScrollController(uiManager_, kSurfaceId); }

    /**
     * A 100x100 page holding a 100x50 multiline field at its top and 300 points of plain content under it, so
     * one point of the page is over the field and another is not.
     */
    void commitScrollViewOverMultilineField(bool scrollEnabled = true) {
        folly::dynamic props = folly::dynamic::object("width", 100)("height", 100);
        const ShadowTreeCommitOptions commitOptions{.enableStateReconciliation = false, .mountSynchronously = true};

        shadowTree_->commit(
            [this, &props, scrollEnabled](const RootShadowNode& oldRootShadowNode) {
                std::vector<std::shared_ptr<const ShadowNode>> children;

                children.push_back(makeMultilineField(21, 100, 50, scrollEnabled));
                children.push_back(makeChild(22, 100, 300));

                return std::static_pointer_cast<RootShadowNode>(oldRootShadowNode.ShadowNode::clone(
                    ShadowNodeFragment{.props = ShadowNodeFragment::propsPlaceholder(),
                                       .children = std::make_shared<const ChildList>(ChildList{
                                           makeConfiguredShadowNode(scrollViewDescriptor_, 20, kSurfaceId,
                                                                    contextContainer_, std::move(props),
                                                                    std::make_shared<const ChildList>(
                                                                        std::move(children)))})}));
            },
            commitOptions);
    }

    /**
     * A controller over the committed ScrollView, scrolled to exactly 100 points by a command rather than by a
     * gesture — so whatever a later commit does to the offset is the only thing left that can move.
     */
    ScrollController makeControllerScrolledToOneHundred() {
        ScrollController controller = makeController();

        controller.dispatchCommands(
            {SceneCommand{.tag = 20, .name = "scrollTo", .args = folly::dynamic::array(0, 100, false)}});
        controller.advance(kFrameMilliseconds60Hz);

        EXPECT_TRUE(controller.hasDispatchedScrollEvent());

        return controller;
    }

    /**
     * A ScrollView whose content view holds one 100-point row per tag, with `maintainVisibleContentPosition`
     * either set or absent. Committing it again with tags in front of the old ones is a prepend, and committing it
     * again without the prop is an application turning the prop off.
     */
    void commitMaintainedScrollView(bool isMaintaining, const std::vector<Tag>& rowTags) {
        folly::dynamic props = folly::dynamic::object("width", 100)("height", 100);

        if (isMaintaining) {
            props["maintainVisibleContentPosition"] = folly::dynamic::object("minIndexForVisible", 0);
        }

        const ShadowTreeCommitOptions commitOptions{.enableStateReconciliation = false, .mountSynchronously = true};

        shadowTree_->commit(
            [this, &props, &rowTags](const RootShadowNode& oldRootShadowNode) {
                return std::static_pointer_cast<RootShadowNode>(oldRootShadowNode.ShadowNode::clone(
                    ShadowNodeFragment{.props = ShadowNodeFragment::propsPlaceholder(),
                                       .children = std::make_shared<const ChildList>(
                                           ChildList{rowList(oldRootShadowNode, props, rowTags)})}));
            },
            commitOptions);
    }

    /**
     * Commits a tall scrolling page and returns a controller over it, then runs one frame. The prologue every
     * scroll-controller test shares - a helper because the same block twice is a jscpd clone at threshold 0.
     */
    ScrollController makeControllerOnTallList() {
        commitScrollView(folly::dynamic::object(), 20000);

        return makeController();
    }

    /**
     * Drives 100 ms frames until the controller stops moving or the budget is spent, and returns the frame
     * count. The settle tail of the gesture-interleave tests, shared for the same cpd reason.
     */
    int settleByDriving(ScrollController& controller) {
        int frames = 0;

        for (; frames < 400 && controller.isScrollActive(); frames++) {
            controller.advance(100.0);
        }

        EXPECT_FALSE(controller.isScrollActive());

        return frames;
    }

    /**
     * Drags, holds the finger still for a frame so the velocity the drag left decays to nothing, then lifts it.
     * That is the motionless release, and it is a helper for the same cpd reason `settleByDriving` is.
     */
    void dragAndReleaseWithoutVelocity(ScrollController& controller) {
        controller.dispatch({drag(40)});
        controller.advance(kFrameMilliseconds60Hz);
        controller.advance(kFrameMilliseconds60Hz);
        controller.dispatch({scrollStop()});
        controller.advance(kFrameMilliseconds60Hz);
    }

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
    std::shared_ptr<const ShadowNode> makeScrollView(Tag tag, folly::dynamic props, double contentHeight) {
        std::vector<std::shared_ptr<const ShadowNode>> children;
        children.push_back(makeChild(21, 100, contentHeight));

        return makeConfiguredShadowNode(scrollViewDescriptor_, tag, kSurfaceId, contextContainer_, std::move(props),
                                        std::make_shared<const ChildList>(std::move(children)));
    }

    std::shared_ptr<const ShadowNode> makeMultilineField(Tag tag, double width, double height, bool scrollEnabled) {
        return makeConfiguredShadowNode(
            textInputDescriptor_, tag, kSurfaceId, contextContainer_,
            folly::dynamic::object("width", width)("height", height)("multiline", true)("scrollEnabled",
                                                                                        scrollEnabled),
            std::make_shared<const ChildList>());
    }

    /**
     * The shape every platform that implements `maintainVisibleContentPosition` measures: a ScrollView holding one
     * content view, whose children are the rows the anchor is chosen from.
     *
     * Everything already in the tree is cloned rather than rebuilt, so each node keeps its family — a controller
     * follows a ScrollView through `getNewestCloneOfShadowNode`, and a node built again from scratch is a
     * different ScrollView wearing the same tag — and keeps the layout Yoga gave it, which a commit that changes
     * no children does not compute again.
     */
    std::shared_ptr<const ShadowNode> rowList(const RootShadowNode& oldRootShadowNode, const folly::dynamic& props,
                                              const std::vector<Tag>& rowTags) {
        const std::shared_ptr<const ShadowNode> oldScrollView =
            oldRootShadowNode.getChildren().empty() ? nullptr : oldRootShadowNode.getChildren().front();
        const std::shared_ptr<const ShadowNode> oldContentView =
            oldScrollView == nullptr ? nullptr : oldScrollView->getChildren().front();
        ChildList rows;

        for (const Tag rowTag : rowTags) {
            rows.push_back(rowNode(oldContentView, rowTag));
        }

        const std::shared_ptr<const ChildList> contentChildren = std::make_shared<const ChildList>(std::move(rows));
        const std::shared_ptr<const ShadowNode> contentView =
            oldContentView == nullptr
                ? makeConfiguredShadowNode(viewDescriptor_, 21, kSurfaceId, contextContainer_,
                                           folly::dynamic::object("width", 100), contentChildren)
                : oldContentView->clone(ShadowNodeFragment{.props = ShadowNodeFragment::propsPlaceholder(),
                                                           .children = contentChildren});
        const std::shared_ptr<const ChildList> scrollViewChildren =
            std::make_shared<const ChildList>(ChildList{contentView});

        if (oldScrollView == nullptr) {
            return makeConfiguredShadowNode(scrollViewDescriptor_, 20, kSurfaceId, contextContainer_,
                                            folly::dynamic(props), scrollViewChildren);
        }

        const PropsParserContext parserContext{kSurfaceId, *contextContainer_};

        return oldScrollView->clone(
            ShadowNodeFragment{.props = scrollViewDescriptor_.cloneProps(parserContext,
                                                                        ScrollViewShadowNode::defaultSharedProps(),
                                                                        RawProps{folly::dynamic(props)}),
                               .children = scrollViewChildren});
    }

    /**
     * The row with this tag as the tree already holds it, or a new one the first time it is committed.
     */
    std::shared_ptr<const ShadowNode> rowNode(const std::shared_ptr<const ShadowNode>& oldContentView, Tag tag) {
        if (oldContentView != nullptr) {
            for (const std::shared_ptr<const ShadowNode>& child : oldContentView->getChildren()) {
                if (child->getTag() == tag) {
                    return child;
                }
            }
        }

        return makeChild(tag, 100, 100);
    }

    std::shared_ptr<const ShadowNode> makeChild(Tag tag, double width, double height) {
        return makeConfiguredShadowNode(viewDescriptor_, tag, kSurfaceId, contextContainer_,
                                        folly::dynamic::object("width", width)("height", height),
                                        std::make_shared<const ChildList>());
    }

    PassThroughShadowTreeDelegate shadowTreeDelegate_;
    std::shared_ptr<const ContextContainer> contextContainer_{std::make_shared<ContextContainer>()};
    ViewComponentDescriptor viewDescriptor_ = makeViewComponentDescriptor(contextContainer_);
    ScrollViewComponentDescriptor scrollViewDescriptor_{ComponentDescriptorParameters{
        .eventDispatcher = EventDispatcher::Shared{}, .contextContainer = contextContainer_, .flavor = nullptr}};
    TextInputComponentDescriptor textInputDescriptor_{ComponentDescriptorParameters{
        .eventDispatcher = EventDispatcher::Shared{}, .contextContainer = contextContainer_, .flavor = nullptr}};

protected:
    void TearDown() override {
        // The registry asserts on destruction while it still holds trees.
        removedTree_ = uiManager_->getShadowTreeRegistry().remove(kSurfaceId);
    }

    std::unique_ptr<ShadowTree> removedTree_;
    std::shared_ptr<UIManager> uiManager_;
};

// Issue #256 and react/core#49226. A multiline <TextInput> is a window on its own content, so it is the deepest
// scrollable under the pointer and the wheel is its rather than the page's — the same deepest-wins rule two
// nested ScrollViews follow. `TextInputController` is what then moves the field; what is asserted here is only
// that the page did not also move, because both moving is the tug-of-war the issue is about.
TEST_F(ScrollControllerTest, AWheelOverAMultilineFieldLeavesTheEnclosingPageAlone) {
    commitScrollViewOverMultilineField();
    ScrollController controller = makeController();

    controller.dispatch({wheel(3, 50.0, 20.0)});
    controller.advance(kFrameMilliseconds60Hz);

    EXPECT_FALSE(controller.hasDispatchedScrollEvent());
    EXPECT_FALSE(controller.isScrollActive());
}

// A field that is not scrollable is not a scrollable, so the walk continues past it to the page that is. The
// prop is routing only: it says nothing about the field's height, which is #114's.
TEST_F(ScrollControllerTest, AWheelOverAFieldWithScrollDisabledIsTheEnclosingPagesAfterAll) {
    commitScrollViewOverMultilineField(false);
    ScrollController controller = makeController();

    controller.dispatch({wheel(3, 50.0, 20.0)});
    controller.advance(kFrameMilliseconds60Hz);

    EXPECT_TRUE(controller.hasDispatchedScrollEvent());
    EXPECT_TRUE(controller.isScrollActive());
}

TEST_F(ScrollControllerTest, AWheelBesideTheFieldStillScrollsTheEnclosingPage) {
    commitScrollViewOverMultilineField();
    ScrollController controller = makeController();

    controller.dispatch({wheel(3, 50.0, 80.0)});
    controller.advance(kFrameMilliseconds60Hz);

    EXPECT_TRUE(controller.hasDispatchedScrollEvent());
    EXPECT_TRUE(controller.isScrollActive());
}

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

TEST_F(ScrollControllerTest, AMotionlessReleaseOnAPagedScrollViewStillHasSomethingLeftToDo) {
    commitScrollView(folly::dynamic::object("pagingEnabled", true));
    ScrollController controller = makeController();

    // A finger that drags and then holds still before it lifts: the frame with no delta behind it leaves the
    // velocity at zero, so the release that follows is the motionless one UIScrollView still pages on.
    dragAndReleaseWithoutVelocity(controller);

    EXPECT_TRUE(controller.isScrollActive());
    EXPECT_TRUE(controller.advance(kFrameMilliseconds60Hz));

    settleByDriving(controller);
}

TEST_F(ScrollControllerTest, AScrollToAfterAReleaseKeepsTheOffsetItAskedFor) {
    commitScrollView(folly::dynamic::object("pagingEnabled", true));
    ScrollController controller = makeController();

    dragAndReleaseWithoutVelocity(controller);

    ASSERT_TRUE(controller.isScrollActive());

    // 37 is nowhere near a 100 point page, so a settle frame that outlived the command would glide the content
    // off the offset the command asked for.
    controller.dispatchCommands(
        {SceneCommand{.tag = 20, .name = "scrollTo", .args = folly::dynamic::array(0, 37, false)}});
    controller.advance(kFrameMilliseconds60Hz);

    EXPECT_TRUE(controller.hasDispatchedScrollEvent());
    EXPECT_FALSE(controller.isScrollActive());

    controller.advance(kFrameMilliseconds60Hz);

    EXPECT_FALSE(controller.hasDispatchedScrollEvent());
}

TEST_F(ScrollControllerTest, AMotionlessReleaseWithNothingToSnapToIsTheEndOfTheGesture) {
    commitScrollView(folly::dynamic::object());
    ScrollController controller = makeController();

    dragAndReleaseWithoutVelocity(controller);

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

TEST_F(ScrollControllerTest, APrependAdjustsTheOffsetOnTheCommitThatMadeIt) {
    commitMaintainedScrollView(true, {31, 32, 33});
    ScrollController controller = makeControllerScrolledToOneHundred();

    commitMaintainedScrollView(true, {41, 42, 31, 32, 33});
    controller.advance(kFrameMilliseconds60Hz);

    EXPECT_TRUE(controller.hasDispatchedScrollEvent());

    // On that commit and on no other frame: the next one has the children it recorded and nothing to adjust for.
    controller.advance(kFrameMilliseconds60Hz);

    EXPECT_FALSE(controller.hasDispatchedScrollEvent());
}

TEST_F(ScrollControllerTest, TurningMaintainVisibleContentPositionOffForgetsTheChildrenItWasWatching) {
    commitMaintainedScrollView(true, {31, 32, 33});
    ScrollController controller = makeControllerScrolledToOneHundred();

    // The commit that turns the prop off is also the commit that prepends, so nothing may be adjusted for it.
    commitMaintainedScrollView(false, {41, 42, 31, 32, 33});
    controller.advance(kFrameMilliseconds60Hz);

    EXPECT_FALSE(controller.hasDispatchedScrollEvent());

    // Back on, against the children it is turned back on with. Measuring those against the ones from before it
    // was turned off would adjust the offset by the prepend a second time.
    commitMaintainedScrollView(true, {41, 42, 31, 32, 33});
    controller.advance(kFrameMilliseconds60Hz);

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

/**
 * A high-resolution wheel's fractional notches accumulate in the queue exactly like whole ones: two
 * quarter-detent events on one axis sum to one half-detent delta, and a stop behind them still ends them.
 */
TEST(ScrollQueueTest, FractionalNotchesFromValue120SumLikeWholeOnes) {
    InputQueue queue;

    queue.push(makeScroll(InputEventKind::PointerScrollDiscrete, ScrollAxisKind::Vertical, 0.25));
    queue.push(makeScroll(InputEventKind::PointerScrollDiscrete, ScrollAxisKind::Vertical, 0.25));
    queue.push(makeScroll(InputEventKind::PointerScrollStop, ScrollAxisKind::Vertical, 0.0));

    const std::vector<InputEvent> events = queue.drain();

    ASSERT_EQ(events.size(), 2U);
    EXPECT_EQ(events[0].kind, InputEventKind::PointerScrollDiscrete);
    EXPECT_EQ(events[0].scrollAxis, ScrollAxisKind::Vertical);
    EXPECT_DOUBLE_EQ(events[0].scrollAmount, 0.5);
    EXPECT_EQ(events[1].kind, InputEventKind::PointerScrollStop);
    EXPECT_EQ(events[1].scrollAxis, ScrollAxisKind::Vertical);
}

#pragma mark - the wheel-versus-touchpad contract (#48)

/**
 * #48: a wheel notch arriving mid-kinetic-gesture accelerates the glide instead of fighting it. The observable
 * is the glide length in 100 ms frames against a control running the identical gesture without the notch - a
 * dropped notch quantizes to the same frame count, so the difference is the assertion.
 */
TEST_F(ScrollControllerTest, AWheelNotchMidKineticGestureAcceleratesTheGlide) {
    const auto framesToSettle = [this](bool withNotch) {
        ScrollController controller = makeControllerOnTallList();

        controller.dispatch({drag(3)});
        controller.dispatch({scrollStop()});

        controller.advance(kFrameMilliseconds60Hz);

        EXPECT_TRUE(controller.isScrollActive());

        // The notch while gliding: same one-frame cadence, and the glide keeps running rather than stopping.
        if (withNotch) {
            controller.dispatch({wheel(3)});
            controller.advance(kFrameMilliseconds60Hz);
        }

        return settleByDriving(controller);
    };

    EXPECT_GT(framesToSettle(true), framesToSettle(false));
}

/**
 * #48: a finger gesture arriving mid-momentum takes over - the tracked path applies the drag one-to-one while
 * the finger is down, and the release starts a fresh momentum from the drag's exit velocity. The observable is
 * the glide length against the momentum-only control: an ignored drag settles in the momentum's frames, while
 * the taken-over drag injects its full 150 points and settles in more.
 */
TEST_F(ScrollControllerTest, AFingerMidMomentumTakesOverAndRunsTheTrackedPath) {
    const auto framesToSettle = [this](bool withFinger) {
        ScrollController controller = makeControllerOnTallList();

        controller.dispatch({wheel(4)});

        controller.advance(kFrameMilliseconds60Hz);

        EXPECT_TRUE(controller.isScrollActive());

        if (withFinger) {
            controller.dispatch({drag(150)});
            controller.dispatch({scrollStop()});
        }

        controller.advance(kFrameMilliseconds60Hz);

        EXPECT_TRUE(controller.hasDispatchedScrollEvent());

        return settleByDriving(controller);
    };

    EXPECT_GT(framesToSettle(true), framesToSettle(false));
}

/**
 * #48: a gesture with no axis_stop still terminates - the wheel path has no stop at all, and the deceleration
 * ends it (pinned with the settle budget the other controller tests use).
 */
TEST_F(ScrollControllerTest, AWheelGestureWithoutAStopStillTerminates) {
    commitScrollView(folly::dynamic::object());
    ScrollController controller = makeController();

    controller.dispatch({wheel(2)});

    bool settled = false;

    for (int frame = 0; frame < 400 && !settled; frame++) {
        settled = !controller.advance(100.0);
    }

    EXPECT_TRUE(settled);

    // One idle frame: the flag describes the last advance, and the frame after settling moved nothing.
    controller.advance(kFrameMilliseconds60Hz);

    EXPECT_FALSE(controller.hasDispatchedScrollEvent());
}

} // namespace
