#include "SerialWorker.h"

#include "ExpoSharingModule.h"

#include <chrono>
#include <cstddef>
#include <future>
#include <gtest/gtest.h>
#include <memory>
#include <thread>
#include <vector>

namespace {

constexpr std::chrono::seconds kTaskBudget{10};

TEST(SerialWorkerTest, TasksRunInOrderOffTheCallingThreadAndTheDestructorRunsThoseStillQueued) {
    std::vector<int> order;
    std::promise<std::thread::id> firstRan;
    std::future<std::thread::id> firstThread = firstRan.get_future();
    { const react_native_linux::SerialWorker idle; }
    {
        react_native_linux::SerialWorker worker;

        worker.post([&] {
            order.push_back(1);
            firstRan.set_value(std::this_thread::get_id());
        });
        EXPECT_NE(firstThread.get(), std::this_thread::get_id());
        for (int task = 2; task <= 4; ++task) {
            worker.post([&order, task] { order.push_back(task); });
        }
    }

    EXPECT_EQ(order, (std::vector<int>{1, 2, 3, 4}));
}

/**
 * The worker destroys a finished task under its lock and then checks the queue before it waits, so a capture that
 * reports its own destruction holds the next `post` until the worker has found the queue empty.
 */
TEST(SerialWorkerTest, ATaskPostedAfterTheQueueRanDryStillRuns) {
    std::promise<void> firstReleased;
    std::future<void> released = firstReleased.get_future();
    std::promise<void> secondRan;
    std::future<void> ran = secondRan.get_future();
    react_native_linux::SerialWorker worker;

    worker.post(
        [signal = std::shared_ptr<void>(nullptr, [&firstReleased](std::nullptr_t) { firstReleased.set_value(); })] {});
    released.wait();
    worker.post([&secondRan] { secondRan.set_value(); });

    EXPECT_EQ(ran.wait_for(kTaskBudget), std::future_status::ready);
}

/**
 * #744: `FileSharing` outlives the JavaScript that awaits its calls, so a portal call still queued when it goes is
 * destroyed unrun, before the destructor waits for the one running, instead of holding the window's close for another
 * portal timeout each. The queued call's capture reports its destruction, which is what lets the running one finish.
 */
TEST(SerialWorkerTest, FileSharingDiscardsTheCallsStillQueuedWhenItGoes) {
    std::promise<void> runningStarted;
    std::future<void> started = runningStarted.get_future();
    std::promise<void> queuedDestroyed;
    std::future<void> destroyed = queuedDestroyed.get_future();
    bool queuedRan = false;
    {
        react_native_linux::FileSharing sharing;

        sharing.worker.post([&runningStarted, &destroyed] {
            runningStarted.set_value();
            EXPECT_EQ(destroyed.wait_for(kTaskBudget), std::future_status::ready);
        });
        started.wait();
        sharing.worker.post([&queuedRan, signal = std::shared_ptr<void>(nullptr, [&queuedDestroyed](std::nullptr_t) {
                                             queuedDestroyed.set_value();
                                         })] { queuedRan = true; });
    }

    EXPECT_FALSE(queuedRan);
}

} // namespace
