#include "StubMessageQueue.h"

#include <atomic>
#include <cstddef>
#include <gtest/gtest.h>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace react_native_linux {
namespace {

constexpr size_t kPostingThreadCount = 4;
constexpr size_t kTasksPerThread = 500;

TEST(StubMessageQueueTest, QueuedTasksRunOnlyWhenFlushedAndInTheOrderTheyWereQueued) {
    StubMessageQueue queue;
    std::vector<std::string> ran;

    queue.runOnQueue([&ran]() { ran.emplace_back("first"); });
    queue.runOnQueue([&ran]() { ran.emplace_back("second"); });

    EXPECT_TRUE(ran.empty());

    queue.flush();

    EXPECT_EQ(ran, (std::vector<std::string>{"first", "second"}));
}

/**
 * `NativeFantom.flushMessageQueue` is a flush called from inside a task the queue is already running: the task it
 * queued runs before the call returns, and the outer flush has nothing left to run twice.
 */
TEST(StubMessageQueueTest, AFlushFromInsideARunningTaskRunsWhatThatTaskQueued) {
    StubMessageQueue queue;
    std::vector<std::string> ran;

    queue.runOnQueue([&queue, &ran]() {
        ran.emplace_back("outer starts");
        queue.runOnQueue([&ran]() { ran.emplace_back("inner"); });
        queue.flush();
        ran.emplace_back("outer ends");
    });

    queue.flush();

    EXPECT_EQ(ran, (std::vector<std::string>{"outer starts", "inner", "outer ends"}));
}

TEST(StubMessageQueueTest, RunOnQueueSyncRunsWhatWasQueuedFirst) {
    StubMessageQueue queue;
    std::vector<std::string> ran;

    queue.runOnQueue([&ran]() { ran.emplace_back("queued"); });
    queue.runOnQueueSync([&ran]() { ran.emplace_back("synchronous"); });

    EXPECT_EQ(ran, (std::vector<std::string>{"queued", "synchronous"}));
}

/**
 * The runtime is destroyed after the queue quits, so a task posted later must be gone at once rather than kept: a
 * kept one would destroy whatever JSI value it holds only when the queue does, after its runtime.
 */
TEST(StubMessageQueueTest, QuitRunsWhatIsQueuedAndDropsWhatIsPostedAfterIt) {
    StubMessageQueue queue;
    std::vector<std::string> ran;

    queue.runOnQueue([&ran]() { ran.emplace_back("before quit"); });
    queue.quitSynchronous();

    const std::shared_ptr<int> heldByLateTask = std::make_shared<int>(0);

    queue.runOnQueue([&ran, heldByLateTask]() { ran.emplace_back("after quit"); });

    EXPECT_EQ(heldByLateTask.use_count(), 1) << "the task posted after quit is still held by the queue";

    queue.flush();

    EXPECT_EQ(ran, (std::vector<std::string>{"before quit"}));
}

/**
 * The timer registry's dispatch thread and the image decode worker post from their own threads while the owning
 * thread flushes. TSan grades the race; the count grades that no task was lost.
 */
TEST(StubMessageQueueTest, TasksPostedFromOtherThreadsWhileTheOwnerFlushesAllRun) {
    StubMessageQueue queue;
    std::atomic<size_t> ranCount{0};
    std::atomic<size_t> finishedThreadCount{0};
    std::vector<std::thread> postingThreads;

    for (size_t thread = 0; thread < kPostingThreadCount; ++thread) {
        postingThreads.emplace_back([&queue, &ranCount, &finishedThreadCount]() {
            for (size_t task = 0; task < kTasksPerThread; ++task) {
                queue.runOnQueue([&ranCount]() { ranCount.fetch_add(1); });
            }

            finishedThreadCount.fetch_add(1);
        });
    }

    while (finishedThreadCount.load() < kPostingThreadCount) {
        queue.flush();
    }

    for (std::thread& postingThread : postingThreads) {
        postingThread.join();
    }

    queue.flush();

    EXPECT_EQ(ranCount.load(), kPostingThreadCount * kTasksPerThread);
}

} // namespace
} // namespace react_native_linux
