#include "SerialWorker.h"

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

} // namespace
