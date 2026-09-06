#include "AnimationFrameQueue.h"
#include "FrameClock.h"

#include <gtest/gtest.h>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using react_native_linux::AnimationFrameQueue;

constexpr double kFirstFrameTimestamp = 16.0;
constexpr double kSecondFrameTimestamp = 32.0;

std::chrono::steady_clock::time_point timeAt(int64_t milliseconds) {
    return std::chrono::steady_clock::time_point(std::chrono::milliseconds(milliseconds));
}

/**
 * Registers a `requestAnimationFrame` loop on `queue`: a callback that counts itself into `tickCount` and asks for
 * the next frame from inside itself, which is the shape every animation loop in the wild has.
 *
 * The callback is returned rather than kept here so the caller owns it for the length of the test, and it names
 * itself through a raw pointer to that owned copy rather than capturing the owner, which would be a cycle.
 */
std::shared_ptr<AnimationFrameQueue::Callback> startSelfRegisteringLoop(AnimationFrameQueue& queue,
                                                                       size_t& tickCount) {
    const std::shared_ptr<AnimationFrameQueue::Callback> loop =
        std::make_shared<AnimationFrameQueue::Callback>();

    *loop = [&queue, &tickCount, self = loop.get()](double /*frameTimestampMilliseconds*/) {
        ++tickCount;
        queue.request(*self);
    };

    queue.request(*loop);

    return loop;
}

/**
 * The whole of what a `requestAnimationFrame` callback does that this queue is responsible for: it says when it
 * ran. Every rule below is an assertion about this vector.
 */
struct FrameTrace {
    std::vector<std::string> entries;

    AnimationFrameQueue::Callback recorder(std::string name) {
        return [this, name = std::move(name)](double frameTimestampMilliseconds) {
            entries.push_back(name + "@" + std::to_string(static_cast<int64_t>(frameTimestampMilliseconds)));
        };
    }
};

TEST(AnimationFrameQueueTest, AFreshQueueHasNothingPendingAndDispatchesNothing) {
    AnimationFrameQueue queue;

    EXPECT_FALSE(queue.hasPendingRequests());
    EXPECT_EQ(queue.dispatchFrame(kFirstFrameTimestamp), 0U);
}

TEST(AnimationFrameQueueTest, ARequestIsPendingUntilTheFrameThatRunsIt) {
    AnimationFrameQueue queue;
    FrameTrace trace;

    queue.request(trace.recorder("only"));

    EXPECT_TRUE(queue.hasPendingRequests());
    EXPECT_EQ(queue.dispatchFrame(kFirstFrameTimestamp), 1U);
    EXPECT_FALSE(queue.hasPendingRequests());
    EXPECT_EQ(trace.entries, std::vector<std::string>{"only@16"});
}

TEST(AnimationFrameQueueTest, CallbacksRunInRegistrationOrderWithOneTimestampForTheFrame) {
    AnimationFrameQueue queue;
    FrameTrace trace;

    for (const std::string& name : {"first", "second", "third", "fourth"}) {
        queue.request(trace.recorder(name));
    }

    EXPECT_EQ(queue.dispatchFrame(kFirstFrameTimestamp), 4U);
    EXPECT_EQ(trace.entries,
              (std::vector<std::string>{"first@16", "second@16", "third@16", "fourth@16"}));
}

/**
 * The scale react-native#48005 was reported at: a heap ordered by deadline alone reorders a thousand
 * same-deadline callbacks, a queue does not.
 */
TEST(AnimationFrameQueueTest, AThousandRegistrationsAllRunOnOneFrameInOrder) {
    constexpr size_t kRegistrationCount = 1000;

    AnimationFrameQueue queue;
    std::vector<size_t> dispatchOrder;

    for (size_t index = 0; index < kRegistrationCount; ++index) {
        queue.request([&dispatchOrder, index](double /*frameTimestampMilliseconds*/) {
            dispatchOrder.push_back(index);
        });
    }

    EXPECT_EQ(queue.dispatchFrame(kFirstFrameTimestamp), kRegistrationCount);
    ASSERT_EQ(dispatchOrder.size(), kRegistrationCount);

    for (size_t index = 0; index < kRegistrationCount; ++index) {
        ASSERT_EQ(dispatchOrder[index], index);
    }
}

TEST(AnimationFrameQueueTest, ARequestMadeInsideACallbackRunsOnTheNextFrame) {
    AnimationFrameQueue queue;
    FrameTrace trace;

    queue.request([&queue, &trace](double /*frameTimestampMilliseconds*/) {
        trace.entries.emplace_back("outer");
        queue.request(trace.recorder("inner"));
    });

    EXPECT_EQ(queue.dispatchFrame(kFirstFrameTimestamp), 1U);
    EXPECT_EQ(trace.entries, std::vector<std::string>{"outer"});
    EXPECT_TRUE(queue.hasPendingRequests());

    EXPECT_EQ(queue.dispatchFrame(kSecondFrameTimestamp), 1U);
    EXPECT_EQ(trace.entries, (std::vector<std::string>{"outer", "inner@32"}));
}

/**
 * The rule that keeps a self-perpetuating loop at frame rate: re-registering from inside the callback advances by
 * exactly one callback per dispatch, never two, however many frames run.
 */
TEST(AnimationFrameQueueTest, ASelfPerpetuatingLoopRunsExactlyOnceEveryFrame) {
    constexpr size_t kFrameCount = 50;

    AnimationFrameQueue queue;
    size_t tickCount = 0;
    const std::shared_ptr<AnimationFrameQueue::Callback> loop = startSelfRegisteringLoop(queue, tickCount);

    for (size_t frame = 0; frame < kFrameCount; ++frame) {
        EXPECT_EQ(queue.dispatchFrame(kFirstFrameTimestamp), 1U);
        EXPECT_EQ(tickCount, frame + 1);
        EXPECT_TRUE(queue.hasPendingRequests());
    }
}

TEST(AnimationFrameQueueTest, CancellingBeforeTheFrameDropsTheCallback) {
    AnimationFrameQueue queue;
    FrameTrace trace;

    queue.request(trace.recorder("kept"));
    const uint64_t cancelled = queue.request(trace.recorder("dropped"));

    queue.cancel(cancelled);

    EXPECT_EQ(queue.dispatchFrame(kFirstFrameTimestamp), 1U);
    EXPECT_EQ(trace.entries, std::vector<std::string>{"kept@16"});
}

TEST(AnimationFrameQueueTest, CancellingTheOnlyRequestLeavesNothingPending) {
    AnimationFrameQueue queue;
    FrameTrace trace;

    queue.cancel(queue.request(trace.recorder("dropped")));

    EXPECT_FALSE(queue.hasPendingRequests());
    EXPECT_EQ(queue.dispatchFrame(kFirstFrameTimestamp), 0U);
    EXPECT_TRUE(trace.entries.empty());
}

/**
 * The case a plain "erase it from the pending list" implementation gets wrong: by the time the callback runs, the
 * request it cancels is no longer pending — it is in the batch this frame is halfway through.
 */
TEST(AnimationFrameQueueTest, ACallbackCanCancelALaterCallbackOfItsOwnFrame) {
    AnimationFrameQueue queue;
    FrameTrace trace;
    uint64_t laterHandle = 0;

    queue.request([&queue, &trace, &laterHandle](double /*frameTimestampMilliseconds*/) {
        trace.entries.emplace_back("first");
        queue.cancel(laterHandle);
    });
    laterHandle = queue.request(trace.recorder("second"));
    queue.request(trace.recorder("third"));

    EXPECT_EQ(queue.dispatchFrame(kFirstFrameTimestamp), 2U);
    EXPECT_EQ(trace.entries, (std::vector<std::string>{"first", "third@16"}));
}

TEST(AnimationFrameQueueTest, ACallbackCanCancelARequestItsOwnFrameAlreadyMadeForTheNextOne) {
    AnimationFrameQueue queue;
    FrameTrace trace;
    uint64_t nextFrameHandle = 0;

    queue.request([&queue, &trace, &nextFrameHandle](double /*frameTimestampMilliseconds*/) {
        nextFrameHandle = queue.request(trace.recorder("never"));
        queue.cancel(nextFrameHandle);
    });

    EXPECT_EQ(queue.dispatchFrame(kFirstFrameTimestamp), 1U);
    EXPECT_NE(nextFrameHandle, 0U);
    EXPECT_FALSE(queue.hasPendingRequests());
    EXPECT_EQ(queue.dispatchFrame(kSecondFrameTimestamp), 0U);
    EXPECT_TRUE(trace.entries.empty());
}

TEST(AnimationFrameQueueTest, ClearingDropsEveryRegisteredCallbackWithoutRunningIt) {
    AnimationFrameQueue queue;
    FrameTrace trace;

    queue.request(trace.recorder("first"));
    queue.request(trace.recorder("second"));
    queue.clear();

    EXPECT_FALSE(queue.hasPendingRequests());
    EXPECT_EQ(queue.dispatchFrame(kFirstFrameTimestamp), 0U);
    EXPECT_TRUE(trace.entries.empty());
}

/**
 * The browsers' rule: an exception in one callback is reported and does not cancel the rest of the frame. The
 * exception still reaches the caller, so the host's error reporting sees it.
 */
TEST(AnimationFrameQueueTest, AThrowingCallbackDoesNotStopTheRestOfItsFrame) {
    AnimationFrameQueue queue;
    FrameTrace trace;

    queue.request(trace.recorder("before"));
    queue.request([](double /*frameTimestampMilliseconds*/) { throw std::runtime_error("callback failed"); });
    queue.request(trace.recorder("after"));

    EXPECT_THROW(queue.dispatchFrame(kFirstFrameTimestamp), std::runtime_error);
    EXPECT_EQ(trace.entries, (std::vector<std::string>{"before@16", "after@16"}));
}

/**
 * The failure mode this test exists for: a dispatch that unwound without cleaning up would strand
 * `hasPendingRequests` at true forever and retain the frame's entries, so the next frame's registrations would run
 * ahead of the stranded ones instead of in their own order.
 */
TEST(AnimationFrameQueueTest, AFrameThatThrewLeavesNothingPendingAndTheNextFrameStillRunsInOrder) {
    AnimationFrameQueue queue;
    FrameTrace trace;

    queue.request([](double /*frameTimestampMilliseconds*/) { throw std::runtime_error("callback failed"); });

    EXPECT_THROW(queue.dispatchFrame(kFirstFrameTimestamp), std::runtime_error);
    EXPECT_FALSE(queue.hasPendingRequests());

    queue.request(trace.recorder("first"));
    queue.request(trace.recorder("second"));

    EXPECT_EQ(queue.dispatchFrame(kSecondFrameTimestamp), 2U);
    EXPECT_EQ(trace.entries, (std::vector<std::string>{"first@32", "second@32"}));
}

/**
 * Only one exception can be rethrown, and it is the first, so the failure a reader is shown is the one that
 * happened first rather than whichever callback happened to be last.
 */
TEST(AnimationFrameQueueTest, TheFirstExceptionOfAFrameIsTheOneThatPropagates) {
    AnimationFrameQueue queue;
    FrameTrace trace;

    queue.request([](double /*frameTimestampMilliseconds*/) { throw std::runtime_error("first failure"); });
    queue.request([](double /*frameTimestampMilliseconds*/) { throw std::logic_error("second failure"); });
    queue.request(trace.recorder("still ran"));

    EXPECT_THROW(
        {
            try {
                queue.dispatchFrame(kFirstFrameTimestamp);
            } catch (const std::runtime_error& failure) {
                EXPECT_STREQ(failure.what(), "first failure");
                throw;
            }
        },
        std::runtime_error);
    EXPECT_EQ(trace.entries, std::vector<std::string>{"still ran@16"});
    EXPECT_FALSE(queue.hasPendingRequests());
}

/**
 * The liveness half without a `WindowSession`: `hasPendingRequests` is the only pending-work signal a bundle that
 * does nothing but re-register produces, and it is what makes `FrameClock` draw on a fallback timeout. A hundred
 * timeouts and not one `wl_surface.frame` — the occluded, inactive-workspace window of ADR-0001 decision 3 — and
 * the loop still advances once per frame. This is the composition `WindowSession::hasPendingWork` performs,
 * exercised directly; a queue that reported idle mid-dispatch, or a clock that ignored the signal, stops here.
 */
TEST(AnimationFrameQueueTest, TheFallbackDeadlineKeepsDrawingWhenOnlyAnAnimationFrameIsPending) {
    constexpr int64_t kFallbackIntervalMilliseconds = 50;
    constexpr int64_t kTimeoutCount = 100;

    AnimationFrameQueue queue;
    react_native_linux::FrameClock clock;
    size_t tickCount = 0;
    const std::shared_ptr<AnimationFrameQueue::Callback> loop = startSelfRegisteringLoop(queue, tickCount);

    for (int64_t timeout = 1; timeout <= kTimeoutCount; ++timeout) {
        const std::chrono::steady_clock::time_point now =
            std::chrono::steady_clock::time_point(std::chrono::milliseconds(timeout * kFallbackIntervalMilliseconds));
        const react_native_linux::FrameClock::Tick frameTick =
            clock.onFallbackTimeout(now, queue.hasPendingRequests());

        ASSERT_TRUE(frameTick.shouldDraw);
        ASSERT_EQ(frameTick.source, react_native_linux::FrameClock::Source::Timer);

        queue.dispatchFrame(static_cast<double>(timeout * kFallbackIntervalMilliseconds));
    }

    EXPECT_EQ(tickCount, static_cast<size_t>(kTimeoutCount));
    EXPECT_EQ(clock.timerTicks(), static_cast<uint64_t>(kTimeoutCount));
    EXPECT_EQ(clock.callbackTicks(), 0U);
    EXPECT_EQ(clock.lastCallbackAt(), std::nullopt);
}

/**
 * The negative control for the test above: once the loop stops re-registering, the same fallback timeout stops
 * drawing, so the pending-work signal is what was driving it rather than the timeout being unconditional.
 */
TEST(AnimationFrameQueueTest, TheFallbackDeadlineStopsDrawingOnceTheLoopStops) {
    AnimationFrameQueue queue;
    react_native_linux::FrameClock clock;
    FrameTrace trace;

    queue.request(trace.recorder("only"));

    EXPECT_TRUE(clock.onFallbackTimeout(timeAt(50), queue.hasPendingRequests()).shouldDraw);

    queue.dispatchFrame(50.0);

    EXPECT_FALSE(clock.onFallbackTimeout(timeAt(100), queue.hasPendingRequests()).shouldDraw);
    EXPECT_EQ(clock.timerTicks(), 1U);
}

TEST(AnimationFrameQueueTest, AnUnknownHandleCancelsNothing) {
    constexpr uint64_t kNeverIssuedHandle = 987654;

    AnimationFrameQueue queue;
    FrameTrace trace;

    queue.request(trace.recorder("kept"));
    queue.cancel(kNeverIssuedHandle);
    queue.cancel(0);

    EXPECT_EQ(queue.dispatchFrame(kFirstFrameTimestamp), 1U);
    EXPECT_EQ(trace.entries, std::vector<std::string>{"kept@16"});
}

TEST(AnimationFrameQueueTest, ACallbackThatAlreadyRanCannotBeCancelledTwice) {
    AnimationFrameQueue queue;
    FrameTrace trace;

    const uint64_t handle = queue.request(trace.recorder("ran"));

    EXPECT_EQ(queue.dispatchFrame(kFirstFrameTimestamp), 1U);

    queue.cancel(handle);

    EXPECT_FALSE(queue.hasPendingRequests());
    EXPECT_EQ(queue.dispatchFrame(kSecondFrameTimestamp), 0U);
    EXPECT_EQ(trace.entries, std::vector<std::string>{"ran@16"});
}

/**
 * Handles are the identity `cancelAnimationFrame` addresses, so they have to keep being distinct across frames —
 * a reused handle would let a stale cancellation drop somebody else's callback.
 */
TEST(AnimationFrameQueueTest, HandlesAreNeverZeroAndNeverReused) {
    AnimationFrameQueue queue;
    FrameTrace trace;
    std::vector<uint64_t> handles;

    handles.push_back(queue.request(trace.recorder("first")));
    handles.push_back(queue.request(trace.recorder("second")));
    queue.dispatchFrame(kFirstFrameTimestamp);
    handles.push_back(queue.request(trace.recorder("third")));
    queue.dispatchFrame(kSecondFrameTimestamp);

    EXPECT_EQ(handles, (std::vector<uint64_t>{1, 2, 3}));
}

/**
 * The liveness half, at the seam `WindowSession::hasPendingWork` reads: a loop that re-registers from inside its
 * own callback is never observed idle, not even by a frame thread that asks in the middle of a dispatch.
 */
TEST(AnimationFrameQueueTest, ALoopIsNeverObservedIdleFromInsideItsOwnCallback) {
    AnimationFrameQueue queue;
    std::vector<bool> observations;

    queue.request([&queue, &observations](double /*frameTimestampMilliseconds*/) {
        observations.push_back(queue.hasPendingRequests());
        queue.request([&queue, &observations](double /*frameTimestampMilliseconds*/) {
            observations.push_back(queue.hasPendingRequests());
        });
        observations.push_back(queue.hasPendingRequests());
    });

    EXPECT_EQ(queue.dispatchFrame(kFirstFrameTimestamp), 1U);
    EXPECT_TRUE(queue.hasPendingRequests());
    EXPECT_EQ(queue.dispatchFrame(kSecondFrameTimestamp), 1U);
    EXPECT_FALSE(queue.hasPendingRequests());
    EXPECT_EQ(observations, (std::vector<bool>{true, true, true}));
}

} // namespace
