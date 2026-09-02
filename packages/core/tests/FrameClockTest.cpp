#include "FrameClock.h"

#include <gtest/gtest.h>

#include <chrono>

namespace {

using react_native_linux::FrameClock;

std::chrono::steady_clock::time_point timeAt(int64_t milliseconds) {
    return std::chrono::steady_clock::time_point(std::chrono::milliseconds(milliseconds));
}

TEST(FrameClockTest, ACallbackAlwaysDrawsAndReportsTheDeltaSinceTheLastTick) {
    FrameClock clock;

    const FrameClock::Tick first = clock.onFrameCallback(timeAt(0));
    const FrameClock::Tick second = clock.onFrameCallback(timeAt(16));

    EXPECT_TRUE(first.shouldDraw);
    EXPECT_EQ(first.source, FrameClock::Source::Callback);
    EXPECT_DOUBLE_EQ(first.deltaMilliseconds, 0.0);
    EXPECT_FALSE(first.resumed);

    EXPECT_TRUE(second.shouldDraw);
    EXPECT_DOUBLE_EQ(second.deltaMilliseconds, 16.0);
    EXPECT_FALSE(second.resumed);
}

TEST(FrameClockTest, ATimeoutWithPendingWorkDrawsFromTheWallClock) {
    FrameClock clock;

    clock.onFrameCallback(timeAt(0));
    const FrameClock::Tick tick = clock.onFallbackTimeout(timeAt(50), true);

    EXPECT_TRUE(tick.shouldDraw);
    EXPECT_EQ(tick.source, FrameClock::Source::Timer);
    EXPECT_DOUBLE_EQ(tick.deltaMilliseconds, 50.0);
    EXPECT_FALSE(tick.resumed);
}

TEST(FrameClockTest, ATimeoutWithNoPendingWorkDoesNotDraw) {
    FrameClock clock;

    clock.onFrameCallback(timeAt(0));
    const FrameClock::Tick tick = clock.onFallbackTimeout(timeAt(50), false);

    EXPECT_FALSE(tick.shouldDraw);
    EXPECT_EQ(clock.timerTicks(), 0U);
}

TEST(FrameClockTest, TheFallbackDoesNotSpinAcrossRepeatedIdleTimeouts) {
    FrameClock clock;

    clock.onFrameCallback(timeAt(0));

    for (int64_t millisecond = 50; millisecond <= 500; millisecond += 50) {
        const FrameClock::Tick tick = clock.onFallbackTimeout(timeAt(millisecond), false);

        EXPECT_FALSE(tick.shouldDraw);
    }

    EXPECT_EQ(clock.timerTicks(), 0U);
    EXPECT_EQ(clock.callbackTicks(), 1U);
}

TEST(FrameClockTest, AnIdleTimeoutDoesNotAdvanceTheReferenceForTheNextDelta) {
    FrameClock clock;

    clock.onFrameCallback(timeAt(0));
    clock.onFallbackTimeout(timeAt(50), false);
    const FrameClock::Tick tick = clock.onFallbackTimeout(timeAt(100), true);

    EXPECT_TRUE(tick.shouldDraw);
    EXPECT_DOUBLE_EQ(tick.deltaMilliseconds, 100.0);
}

TEST(FrameClockTest, TheFirstCallbackAfterFallbackTicksResumesExactlyOnce) {
    FrameClock clock;

    clock.onFrameCallback(timeAt(0));
    clock.onFallbackTimeout(timeAt(50), true);
    clock.onFallbackTimeout(timeAt(100), true);
    const FrameClock::Tick resumingTick = clock.onFrameCallback(timeAt(116));
    const FrameClock::Tick nextTick = clock.onFrameCallback(timeAt(132));

    EXPECT_TRUE(resumingTick.resumed);
    EXPECT_DOUBLE_EQ(resumingTick.deltaMilliseconds, 16.0);
    EXPECT_FALSE(nextTick.resumed);
    EXPECT_EQ(clock.resumeTransitions(), 1U);
}

TEST(FrameClockTest, ACallbackArrivingDuringTheFallbackWindowTakesOverImmediately) {
    FrameClock clock;

    clock.onFrameCallback(timeAt(0));
    clock.onFallbackTimeout(timeAt(50), true);
    const FrameClock::Tick callbackDuringFallback = clock.onFrameCallback(timeAt(66));

    EXPECT_TRUE(callbackDuringFallback.shouldDraw);
    EXPECT_EQ(callbackDuringFallback.source, FrameClock::Source::Callback);
    EXPECT_TRUE(callbackDuringFallback.resumed);
}

TEST(FrameClockTest, CountersTrackEachSourceAndTheLastCallbackTime) {
    FrameClock clock;

    EXPECT_EQ(clock.lastCallbackAt(), std::nullopt);

    clock.onFrameCallback(timeAt(0));
    clock.onFrameCallback(timeAt(16));
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

} // namespace
