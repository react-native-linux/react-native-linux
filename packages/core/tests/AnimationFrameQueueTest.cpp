#include "AnimationFrameQueue.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace {

using react_native_linux::AnimationFrameQueue;

constexpr double kFirstFrameTimestamp = 16.0;
constexpr double kSecondFrameTimestamp = 32.0;

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
    AnimationFrameQueue::Callback tick;

    tick = [&queue, &tickCount, &tick](double /*frameTimestampMilliseconds*/) {
        ++tickCount;
        queue.request(tick);
    };

    queue.request(tick);

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
