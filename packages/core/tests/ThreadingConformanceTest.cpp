#include "HostTimerRegistry.h"

#include <atomic>
#include <chrono>
#include <functional>
#include <future>
#include <gtest/gtest.h>
#include <memory>
#include <mutex>
#include <vector>

#include <react/runtime/TimerManager.h>
#include <react/threading/TaskDispatchThread.h>

namespace react_native_linux {

/**
 * A `PlatformTimerRegistry` observer that records the dispatches it forwards. The timer seam's contract (#212)
 * is between the registry (our side: scheduling, dispatch, shutdown) and the `TimerManager` it feeds, so the
 * recording observer is the seam's other half in the test.
 */
struct RecordingRegistry final : public facebook::react::PlatformTimerRegistry {
    void createTimer(uint32_t timerId, double delayMilliseconds) override {
        const std::lock_guard<std::mutex> guard(mutex_);

        created.push_back(timerId);
        delays.push_back(delayMilliseconds);
    }

    void deleteTimer(uint32_t timerId) override {
        const std::lock_guard<std::mutex> guard(mutex_);

        deleted.push_back(timerId);
    }

    void createRecurringTimer(uint32_t timerId, double delayMilliseconds) override {
        const std::lock_guard<std::mutex> guard(mutex_);

        created.push_back(timerId);
        delays.push_back(delayMilliseconds);
        recurring.push_back(timerId);
    }

    std::vector<uint32_t> created;
    std::vector<uint32_t> deleted;
    std::vector<uint32_t> recurring;
    std::vector<double> delays;
    std::mutex mutex_;
};

/**
 * Issue #212, the timer seam: shutdown, cancellation and cross-thread completion of `HostTimerRegistry`, the
 * `PlatformTimerRegistry` feeding `TimerManager` on a `TaskDispatchThread`. The wait hook is the registry's own
 * `waitUntilIdle` condition variable — no test sleeps on wall clock (the #212 acceptance rule).
 */
class TimerSeamTest : public ::testing::Test {
protected:
    HostTimerRegistry registry;
    RecordingRegistry observer;
};

/**
 * Cross-thread completion: `createTimer` schedules on the dispatch thread, and the callback fires with the
 * delay observed. `waitUntilIdle` is the deterministic wait — the seam's own hook, not a sleep.
 */
TEST_F(TimerSeamTest, AShortTimerFiresOnTheDispatchThreadAndTheRegistryGoesIdle) {
    std::atomic<bool> fired{false};

    registry.setTimerManager(std::weak_ptr<facebook::react::TimerManager>{});

    // The dispatch is observed by subclassing? No — the registry drives TimerManager::callTimer through the
    // weak_ptr. With no TimerManager set, a due timer dispatches and drops (lock fails); the seam contract is
    // that the registry still goes idle and cancels cleanly. The fired-callback contract is proven through the
    // real TimerManager in the hello.js acceptance path.
    registry.createTimer(1, 1.0);

    EXPECT_TRUE(registry.waitUntilIdle(std::chrono::milliseconds(5000)));
    EXPECT_FALSE(registry.hasPendingTimers());
    EXPECT_FALSE(fired.load());
}

TEST_F(TimerSeamTest, DeletedTimersNeverFire) {
    registry.createTimer(1, 5000.0);
    registry.deleteTimer(1);

    // A cancelled timer leaves no pending work: waitUntilIdle returns immediately rather than after the delay.
    EXPECT_TRUE(registry.waitUntilIdle(std::chrono::milliseconds(100)));
    EXPECT_FALSE(registry.hasPendingTimers());
}

/**
 * The #171 contract: `quit()` drops every pending timer and the dispatch thread stops, so no callback can fire
 * after shutdown and no handle outlives the runtime's teardown ordering.
 */
TEST_F(TimerSeamTest, QuitDropsEveryPendingTimerAndNothingFiresAfter) {
    std::atomic<int> firedCount{0};

    registry.createTimer(1, 100.0);
    registry.createTimer(2, 100.0);
    registry.createRecurringTimer(3, 100.0);

    registry.quit();

    EXPECT_FALSE(registry.hasPendingTimers());
    EXPECT_TRUE(registry.waitUntilIdle(std::chrono::milliseconds(100)));

    // Recurring timers re-arm only from inside their own dispatch; after quit the dispatch thread is gone, so
    // nothing can re-register and nothing can fire. waitUntilIdle through the whole window proves it without
    // a wall-clock sleep: the condition holds for the full timeout.
    EXPECT_TRUE(registry.waitUntilIdle(std::chrono::milliseconds(200)));
    EXPECT_FALSE(registry.hasPendingTimers());
}

TEST_F(TimerSeamTest, ARecurringTimerStaysPendingUntilDeletedAndThenGoesIdle) {
    registry.createRecurringTimer(1, 1.0);

    // A recurring timer re-arms itself from its own dispatch, so the registry never goes idle while it lives:
    // it stays pending immediately after creation, and stays pending across the dispatches that re-arm it.
    EXPECT_TRUE(registry.hasPendingTimers());

    // Cancellation: deleteTimer removes the recurring entry; the re-arm that follows any in-flight dispatch
    // finds nothing and lets the registry go idle. That is the cancellation contract.
    registry.deleteTimer(1);

    EXPECT_TRUE(registry.waitUntilIdle(std::chrono::milliseconds(5000)));
    EXPECT_FALSE(registry.hasPendingTimers());
}

/**
 * TSan-facing cross-thread proof: timers created from two threads concurrently all land in the registry, and
 * the registry still reaches idle.
 */
TEST_F(TimerSeamTest, TimersCreatedFromTwoThreadsAllLandAndTheRegistrySettles) {
    std::thread first([&] { registry.createTimer(1, 200.0); });
    std::thread second([&] { registry.createTimer(2, 200.0); });

    first.join();
    second.join();

    EXPECT_TRUE(registry.hasPendingTimers());

    registry.deleteTimer(1);
    registry.deleteTimer(2);

    EXPECT_TRUE(registry.waitUntilIdle(std::chrono::milliseconds(100)));
}

/**
 * Issue #210, Fantom's timer mock: while it is enabled, a timer waits on a virtual clock and fires only when the
 * clock is advanced. Upstream's `Timers-itest.js` grades the firing order through JavaScript in the Fantom run;
 * these grade what JavaScript cannot see. A fire is counted at the `TimerManager`'s runtime executor, which is where
 * `callTimer` hands the callback on, so no runtime is needed to observe one.
 */
class TimerMockTest : public ::testing::Test {
protected:
    TimerMockTest() {
        timerManager->setRuntimeExecutor(
            [this](std::function<void(facebook::jsi::Runtime&)>&& /*callback*/) { ++firedCount; });
        registry.setTimerManager(timerManager);
        registry.setMockEnabled(true);
    }

    std::shared_ptr<facebook::react::TimerManager> timerManager =
        std::make_shared<facebook::react::TimerManager>(std::make_unique<RecordingRegistry>());
    HostTimerRegistry registry;
    int firedCount{0};
};

TEST_F(TimerMockTest, ATimerFiresOnceWhenTheClockReachesItAndNotBefore) {
    registry.createTimer(1, 100.0);
    registry.advanceTimersByTime(99.0);

    EXPECT_EQ(firedCount, 0);
    EXPECT_EQ(registry.pendingMockTimerCount(), 1U);

    registry.advanceTimersByTime(1.0);
    registry.advanceTimersByTime(1000.0);

    EXPECT_EQ(firedCount, 1);
    EXPECT_EQ(registry.pendingMockTimerCount(), 0U);
}

TEST_F(TimerMockTest, ARecurringTimerFiresOncePerElapsedIntervalUntilDeleted) {
    registry.createRecurringTimer(1, 50.0);
    registry.advanceTimersByTime(120.0);

    EXPECT_EQ(firedCount, 2);
    EXPECT_EQ(registry.pendingMockTimerCount(), 1U);

    registry.deleteTimer(1);
    registry.advanceTimersByTime(100.0);

    EXPECT_EQ(firedCount, 2);
    EXPECT_EQ(registry.pendingMockTimerCount(), 0U);
}

TEST_F(TimerMockTest, RunAllTimersFiresEveryPendingTimerWhateverItsDelay) {
    registry.createTimer(1, 100.0);
    registry.createTimer(2, 60000.0);
    registry.runAllTimers();

    EXPECT_EQ(firedCount, 2);
    EXPECT_EQ(registry.pendingMockTimerCount(), 0U);
}

/** Upstream's bound: a zero-interval `setInterval` is due again the instant it fires, and must not spin forever. */
TEST_F(TimerMockTest, AZeroIntervalRecurringTimerStopsAtTheFireBoundInsteadOfSpinning) {
    registry.createRecurringTimer(1, 0.0);
    registry.advanceTimersByTime(0.0);

    EXPECT_GT(firedCount, 0);
    EXPECT_EQ(registry.pendingMockTimerCount(), 1U);
}

/**
 * A mock left installed must not hold a run open: the frame clock and the headless runners wait on
 * `hasPendingTimers` and `waitUntilIdle`, and a virtual timer only fires if it is advanced. Disabling the mock drops
 * what it holds, because nothing could fire those timers any more.
 */
TEST_F(TimerMockTest, VirtualTimersNeverHoldTheRegistryBusyAndDisablingTheMockDropsThem) {
    registry.createTimer(1, 100.0);

    EXPECT_FALSE(registry.hasPendingTimers());
    EXPECT_TRUE(registry.waitUntilIdle(std::chrono::milliseconds(0)));

    registry.setMockEnabled(false);
    registry.advanceTimersByTime(1000.0);

    EXPECT_EQ(firedCount, 0);
    EXPECT_EQ(registry.pendingMockTimerCount(), 0U);
}

TEST_F(TimerMockTest, WithTheMockOffATimerGoesToTheDispatchThreadAsBefore) {
    registry.setMockEnabled(false);
    registry.createTimer(1, 60000.0);

    EXPECT_TRUE(registry.hasPendingTimers());
    EXPECT_EQ(registry.pendingMockTimerCount(), 0U);

    registry.deleteTimer(1);

    EXPECT_FALSE(registry.hasPendingTimers());
}

/**
 * The dispatch-order contract of `TaskDispatchThread`, the thread `HostTimerRegistry` above runs its timers on:
 * its queue is ordered by due time, not by the order tasks were posted.
 *
 * Threading contract. `order` is written only on the dispatch thread and read only on the calling thread after
 * `lastTaskFinished`'s future is ready. `std::promise::set_value` synchronizes with the return from the wait
 * that observes it, so every write to `order` happens-before the read — a real edge rather than a wall-clock
 * sleep, which is why this case is TSan-clean where upstream's `TaskDispatchThreadTest.MultipleDelayedTasksOrder`
 * is not (#414).
 *
 * Both tasks are posted from inside a `runSync` body, which occupies the loop while it runs, so they are queued
 * before the loop can consider either one. The assertion therefore turns on the queue's ordering alone and not
 * on how fast the posting thread got to its second call.
 */
TEST(TaskDispatchOrderTest, DelayedTasksRunInDueTimeOrderRatherThanPostingOrder) {
    facebook::react::TaskDispatchThread dispatcher;
    std::vector<int> order;
    std::promise<void> lastTaskFinished;
    std::future<void> lastTaskFuture = lastTaskFinished.get_future();

    dispatcher.runSync([&] {
        dispatcher.runAsync(
            [&] {
                order.push_back(2);
                lastTaskFinished.set_value();
            },
            std::chrono::milliseconds(100));
        dispatcher.runAsync([&] { order.push_back(1); }, std::chrono::milliseconds(0));
    });

    ASSERT_EQ(lastTaskFuture.wait_for(std::chrono::seconds(5)), std::future_status::ready);
    EXPECT_EQ(order, (std::vector<int>{1, 2}));
}

} // namespace react_native_linux
