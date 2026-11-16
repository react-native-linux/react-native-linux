#include "FrameClock.h"

#include <algorithm>
#include <chrono>
#include <gtest/gtest.h>
#include <optional>

namespace {

using react_native_linux::FrameClock;

std::chrono::steady_clock::time_point timeAt(int64_t milliseconds) {
    return std::chrono::steady_clock::time_point(std::chrono::milliseconds(milliseconds));
}

TEST(FrameClockTest, ACallbackWithPendingWorkDrawsAndReportsTheDeltaSinceTheLastTick) {
    FrameClock clock;

    const FrameClock::Tick first = clock.onFrameCallback(timeAt(0), true);
    const FrameClock::Tick second = clock.onFrameCallback(timeAt(16), true);

    EXPECT_TRUE(first.shouldDraw);
    EXPECT_EQ(first.source, FrameClock::Source::Callback);
    EXPECT_DOUBLE_EQ(first.deltaMilliseconds, 0.0);
    EXPECT_FALSE(first.resumed);

    EXPECT_TRUE(second.shouldDraw);
    EXPECT_DOUBLE_EQ(second.deltaMilliseconds, 16.0);
    EXPECT_FALSE(second.resumed);
}

/**
 * #703: once the work runs out a callback still draws for `kSettleCallbacks` frames, so every swapchain image takes
 * the last change, and then skips, which is what stops the presents because only a draw re-arms `wl_surface.frame`.
 * The skip moves neither a counter nor the delta reference, so the tick that wakes the window on the fallback timeout
 * measures from the last frame it actually drew, and work restarts the settle.
 */
TEST(FrameClockTest, IdleCallbacksSettleThenSkipAndTheNextWakeMeasuresFromTheLastDraw) {
    FrameClock clock;

    clock.onFrameCallback(timeAt(0), true);
    const FrameClock::Tick firstSettle = clock.onFrameCallback(timeAt(8), false);
    const FrameClock::Tick secondSettle = clock.onFrameCallback(timeAt(16), false);
    const FrameClock::Tick idle = clock.onFrameCallback(timeAt(24), false);
    const FrameClock::Tick wake = clock.onFallbackTimeout(timeAt(56), true);

    EXPECT_TRUE(firstSettle.shouldDraw);
    EXPECT_TRUE(secondSettle.shouldDraw);
    EXPECT_FALSE(idle.shouldDraw);
    EXPECT_EQ(idle.source, FrameClock::Source::Callback);
    EXPECT_EQ(clock.callbackTicks(), 3U);
    EXPECT_EQ(clock.lastCallbackAt(), timeAt(16));
    EXPECT_TRUE(wake.shouldDraw);
    EXPECT_EQ(wake.source, FrameClock::Source::Timer);
    EXPECT_DOUBLE_EQ(wake.deltaMilliseconds, 40.0);
    EXPECT_TRUE(clock.onFrameCallback(timeAt(64), false).shouldDraw);
    EXPECT_TRUE(clock.onFrameCallback(timeAt(72), false).shouldDraw);
    EXPECT_FALSE(clock.onFrameCallback(timeAt(80), false).shouldDraw);
}

/**
 * #703: a draw that presented nothing is owed another one even on a settled clock, from either frame source, and the
 * next draw that presents ends the retry.
 */
TEST(FrameClockTest, ADrawThatPresentedNothingIsRetriedUntilOnePresents) {
    FrameClock clock;

    clock.recordDrawOutcome(false);

    EXPECT_TRUE(clock.onFrameCallback(timeAt(0), false).shouldDraw);
    EXPECT_TRUE(clock.onFallbackTimeout(timeAt(16), false).shouldDraw);

    clock.recordDrawOutcome(true);

    EXPECT_FALSE(clock.onFallbackTimeout(timeAt(32), false).shouldDraw);
}

TEST(FrameClockTest, AClockThatNeverHadWorkIsAlreadySettled) {
    FrameClock clock;

    EXPECT_FALSE(clock.onFrameCallback(timeAt(0), false).shouldDraw);
    EXPECT_EQ(FrameClock::kSettleCallbacks, 2U);
}

TEST(FrameClockTest, ATimeoutWithPendingWorkDrawsFromTheWallClock) {
    FrameClock clock;

    clock.onFrameCallback(timeAt(0), true);
    const FrameClock::Tick tick = clock.onFallbackTimeout(timeAt(50), true);

    EXPECT_TRUE(tick.shouldDraw);
    EXPECT_EQ(tick.source, FrameClock::Source::Timer);
    EXPECT_DOUBLE_EQ(tick.deltaMilliseconds, 50.0);
    EXPECT_FALSE(tick.resumed);
}

TEST(FrameClockTest, ATimeoutWithNoPendingWorkDoesNotDraw) {
    FrameClock clock;

    clock.onFrameCallback(timeAt(0), true);
    const FrameClock::Tick tick = clock.onFallbackTimeout(timeAt(50), false);

    EXPECT_FALSE(tick.shouldDraw);
    EXPECT_EQ(clock.timerTicks(), 0U);
}

TEST(FrameClockTest, TheFallbackDoesNotSpinAcrossRepeatedIdleTimeouts) {
    FrameClock clock;

    clock.onFrameCallback(timeAt(0), true);

    for (int64_t millisecond = 50; millisecond <= 500; millisecond += 50) {
        const FrameClock::Tick tick = clock.onFallbackTimeout(timeAt(millisecond), false);

        EXPECT_FALSE(tick.shouldDraw);
    }

    EXPECT_EQ(clock.timerTicks(), 0U);
    EXPECT_EQ(clock.callbackTicks(), 1U);
}

TEST(FrameClockTest, AnIdleTimeoutDoesNotAdvanceTheReferenceForTheNextDelta) {
    FrameClock clock;

    clock.onFrameCallback(timeAt(0), true);
    clock.onFallbackTimeout(timeAt(50), false);
    const FrameClock::Tick tick = clock.onFallbackTimeout(timeAt(100), true);

    EXPECT_TRUE(tick.shouldDraw);
    EXPECT_DOUBLE_EQ(tick.deltaMilliseconds, 100.0);
}

TEST(FrameClockTest, TheFirstCallbackAfterFallbackTicksResumesExactlyOnce) {
    FrameClock clock;

    clock.onFrameCallback(timeAt(0), true);
    clock.onFallbackTimeout(timeAt(50), true);
    clock.onFallbackTimeout(timeAt(100), true);
    const FrameClock::Tick resumingTick = clock.onFrameCallback(timeAt(116), true);
    const FrameClock::Tick nextTick = clock.onFrameCallback(timeAt(132), true);

    EXPECT_TRUE(resumingTick.resumed);
    EXPECT_DOUBLE_EQ(resumingTick.deltaMilliseconds, 16.0);
    EXPECT_FALSE(nextTick.resumed);
    EXPECT_EQ(clock.resumeTransitions(), 1U);
}

TEST(FrameClockTest, ACallbackArrivingDuringTheFallbackWindowTakesOverImmediately) {
    FrameClock clock;

    clock.onFrameCallback(timeAt(0), true);
    clock.onFallbackTimeout(timeAt(50), true);
    const FrameClock::Tick callbackDuringFallback = clock.onFrameCallback(timeAt(66), true);

    EXPECT_TRUE(callbackDuringFallback.shouldDraw);
    EXPECT_EQ(callbackDuringFallback.source, FrameClock::Source::Callback);
    EXPECT_TRUE(callbackDuringFallback.resumed);
}

TEST(FrameClockTest, CountersTrackEachSourceAndTheLastCallbackTime) {
    FrameClock clock;

    EXPECT_EQ(clock.lastCallbackAt(), std::nullopt);

    clock.onFrameCallback(timeAt(0), true);
    clock.onFrameCallback(timeAt(16), true);
    clock.onFallbackTimeout(timeAt(66), true);
    clock.onFallbackTimeout(timeAt(116), false);

    EXPECT_EQ(clock.callbackTicks(), 2U);
    EXPECT_EQ(clock.timerTicks(), 1U);
    EXPECT_EQ(clock.lastCallbackAt(), timeAt(16));
}

TEST(FrameClockTest, APermanentlySilentFrameSourceIsDetectableFromTheCounters) {
    FrameClock clock;

    for (int64_t millisecond = 0; millisecond <= 1000; millisecond += 50) {
        clock.onFallbackTimeout(timeAt(millisecond), true);
    }

    EXPECT_EQ(clock.callbackTicks(), 0U);
    EXPECT_GT(clock.timerTicks(), 0U);
    EXPECT_EQ(clock.resumeTransitions(), 0U);
    EXPECT_EQ(clock.lastCallbackAt(), std::nullopt);
}

TEST(FrameClockTest, TheFallbackIntervalStretchesWithInactivityOverTheDisplayOwnPeriod) {
    using namespace react_native_linux;

    const std::chrono::nanoseconds refresh{8'333'333};

    EXPECT_EQ(
        FrameClock::fallbackInterval(FrameClock::WindowActivity::Active, FrameClock::ThermalState::Nominal, refresh),
        refresh);
    EXPECT_EQ(
        FrameClock::fallbackInterval(FrameClock::WindowActivity::Inactive, FrameClock::ThermalState::Nominal, refresh),
        refresh * 3);
    EXPECT_EQ(
        FrameClock::fallbackInterval(FrameClock::WindowActivity::Occluded, FrameClock::ThermalState::Nominal, refresh),
        refresh * 8);
}

TEST(FrameClockTest, TheFallbackIntervalStretchesFromTheDisplaySPeriodNotFromAFixedConstant) {
    using namespace react_native_linux;

    const std::chrono::nanoseconds refresh60{16'666'666};

    EXPECT_EQ(FrameClock::fallbackInterval(FrameClock::WindowActivity::Inactive, FrameClock::ThermalState::Nominal,
                                           refresh60),
              refresh60 * 3);
}

TEST(FrameClockTest, ASuspendedWindowIsOccludedWhateverItsActivationAndAnyOtherFollowsIt) {
    using react_native_linux::ToplevelState;

    EXPECT_EQ(FrameClock::windowActivity(ToplevelState{.activated = true}), FrameClock::WindowActivity::Active);
    EXPECT_EQ(FrameClock::windowActivity(ToplevelState{}), FrameClock::WindowActivity::Inactive);
    EXPECT_EQ(FrameClock::windowActivity(ToplevelState{.suspended = true}), FrameClock::WindowActivity::Occluded);
    EXPECT_EQ(FrameClock::windowActivity(ToplevelState{.activated = true, .suspended = true}),
              FrameClock::WindowActivity::Occluded);
}

TEST(FrameClockTest, TheFallbackLineNamesTheActivityItsIntervalAndWhen) {
    EXPECT_EQ(FrameClock::formatFallbackLine(FrameClock::WindowActivity::Active, std::chrono::nanoseconds{8'333'333},
                                             timeAt(2)),
              R"({"fallbackNs":8333333,"activity":"active","atNs":2000000})");
    EXPECT_EQ(
        FrameClock::formatFallbackLine(FrameClock::WindowActivity::Inactive, std::chrono::nanoseconds{25}, timeAt(0)),
        R"({"fallbackNs":25,"activity":"inactive","atNs":0})");
    EXPECT_EQ(
        FrameClock::formatFallbackLine(FrameClock::WindowActivity::Occluded, std::chrono::nanoseconds{66}, timeAt(0)),
        R"({"fallbackNs":66,"activity":"occluded","atNs":0})");
}

/** #335: heat is a minimum frame interval of one 60 Hz frame, so it never wakes a window sooner than nominal. */
TEST(FrameClockTest, ASeriousOrCriticalThermalStateRaisesTheIntervalToOne60HzFrameAndNeverShortensIt) {
    const std::chrono::nanoseconds frame60{16'666'666};

    for (const std::chrono::nanoseconds refresh :
         {std::chrono::nanoseconds{6'944'444}, std::chrono::nanoseconds{8'333'333}, frame60,
          std::chrono::nanoseconds{33'333'333}}) {
        for (const FrameClock::WindowActivity activity :
             {FrameClock::WindowActivity::Active, FrameClock::WindowActivity::Inactive,
              FrameClock::WindowActivity::Occluded}) {
            const std::chrono::nanoseconds nominal =
                FrameClock::fallbackInterval(activity, FrameClock::ThermalState::Nominal, refresh);

            for (const FrameClock::ThermalState hot :
                 {FrameClock::ThermalState::Serious, FrameClock::ThermalState::Critical}) {
                EXPECT_EQ(FrameClock::fallbackInterval(activity, hot, refresh), std::max(nominal, frame60));
            }
        }
    }
}

TEST(FrameClockTest, HeatSlowsAnIdleActive120HzPollAndLeavesAnOccludedWindowStretched) {
    const std::chrono::nanoseconds refresh120{8'333'333};

    EXPECT_EQ(
        FrameClock::fallbackInterval(FrameClock::WindowActivity::Active, FrameClock::ThermalState::Serious, refresh120),
        std::chrono::nanoseconds{16'666'666});
    EXPECT_EQ(FrameClock::fallbackInterval(FrameClock::WindowActivity::Occluded, FrameClock::ThermalState::Critical,
                                           refresh120),
              refresh120 * 8);
}

/**
 * #703: an idle window, which armed no callback, polls for work at the display's period, while one whose last draw
 * armed a callback gives it two periods and never less than a 60 Hz frame before a timer draws instead.
 */
TEST(FrameClockTest, ThePacingPeriodPollsAnIdleWindowAtTheDisplayRateAndLetsAnArmedCallbackRunLate) {
    const std::chrono::nanoseconds nominal{16'666'666};
    const std::chrono::nanoseconds refresh144{6'944'444};
    const std::chrono::nanoseconds refresh30{33'333'333};

    EXPECT_EQ(FrameClock::pacingPeriod(false, refresh144), refresh144);
    EXPECT_EQ(FrameClock::pacingPeriod(true, refresh144), nominal);
    EXPECT_EQ(FrameClock::pacingPeriod(true, refresh30), refresh30 * 2);
    EXPECT_EQ(FrameClock::pacingPeriod(false, std::nullopt), nominal);
    EXPECT_EQ(FrameClock::pacingPeriod(true, std::nullopt), nominal);
}

} // namespace
