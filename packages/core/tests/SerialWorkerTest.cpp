#include "SerialWorker.h"

#include <future>
#include <gtest/gtest.h>
#include <thread>
#include <vector>

namespace {

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

} // namespace
