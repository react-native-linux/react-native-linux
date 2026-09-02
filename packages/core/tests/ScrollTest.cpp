#include "InputPipeline.h"
#include "ScrollPhysics.h"

#include <gtest/gtest.h>

#include <react/renderer/graphics/Point.h>

#include <cstddef>
#include <vector>

namespace {

using facebook::react::Point;
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
using react_native_linux::PointerDispatch;
using react_native_linux::PointerRouter;
using react_native_linux::ScrollAxisKind;
using react_native_linux::ScrollAxisState;
using react_native_linux::velocityForTravel;

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

TEST(ScrollPhysicsTest, ClampsBetweenZeroAndTheContentThatDoesNotFit) {
    EXPECT_DOUBLE_EQ(maximumScrollOffset(kContentLength, kViewportLength), kMaximumOffset);
    EXPECT_DOUBLE_EQ(maximumScrollOffset(100.0, kViewportLength), 0.0);
    EXPECT_DOUBLE_EQ(clampScrollOffset(-30.0, kContentLength, kViewportLength), 0.0);
    EXPECT_DOUBLE_EQ(clampScrollOffset(400.0, kContentLength, kViewportLength), kMaximumOffset);
    EXPECT_DOUBLE_EQ(clampScrollOffset(200.0, kContentLength, kViewportLength), 200.0);
}

TEST(ScrollPhysicsTest, DecaysVelocityByTheRateRaisedToTheFrameTime) {
    const ScrollAxisState normal =
        decelerateAxis(ScrollAxisState{.offset = 0.0, .velocity = 1.0}, kFrameMilliseconds60Hz,
                       kDecelerationRateNormal, kUnboundedContent, kViewportLength);
    const ScrollAxisState fast = decelerateAxis(ScrollAxisState{.offset = 0.0, .velocity = 1.0},
                                                kFrameMilliseconds60Hz, kDecelerationRateFast, kUnboundedContent,
                                                kViewportLength);

    EXPECT_NEAR(normal.velocity, kNormalDecayPerFrame60Hz, kDecayTolerance);
    EXPECT_NEAR(fast.velocity, kFastDecayPerFrame60Hz, kDecayTolerance);
}

TEST(ScrollPhysicsTest, TravelsExactlyTheDistanceAWheelNotchAsksFor) {
    const double travelled =
        settledOffset(velocityForTravel(kWheelNotchDistance, kDecelerationRateNormal), kFrameMilliseconds60Hz,
                      kDecelerationRateNormal);

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
    const ScrollAxisState advanced = decelerateAxis(
        ScrollAxisState{.offset = 318.0, .velocity = velocityForTravel(200.0, kDecelerationRateNormal)},
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
                                            InputEventKind::PointerScrollDiscrete,
                                            InputEventKind::PointerScrollStop};

    for (InputEventKind kind : kinds) {
        const std::vector<PointerDispatch> dispatches =
            router.route(makeScroll(kind, ScrollAxisKind::Vertical, 1.0), 4, Point{});

        EXPECT_TRUE(dispatches.empty());
    }
}

} // namespace
