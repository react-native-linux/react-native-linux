#include "LinuxUIScheduler.h"

#include <cstddef>
#include <gtest/gtest.h>
#include <memory>
#include <thread>
#include <vector>

// Every scheduler here is constructed on the test's own thread: upstream's `isOnUIThread` caches its answer in one
// thread_local shared by every `UIScheduler`, which holds because a process has exactly one UI thread.

namespace react_native_linux {
namespace {

constexpr int kSchedulingThreadCount = 4;
constexpr int kJobsPerThread = 1000;

TEST(LinuxUISchedulerTest, OnlyTheFrameThreadThatConstructedItIsTheUIThread) {
    const LinuxUIScheduler scheduler;
    bool isUIThreadElsewhere = true;

    std::thread([&scheduler, &isUIThreadElsewhere] { isUIThreadElsewhere = scheduler.isOnUIThread(); }).join();

    EXPECT_TRUE(scheduler.isOnUIThread());
    EXPECT_FALSE(isUIThreadElsewhere);
}

TEST(LinuxUISchedulerTest, AJobScheduledOnTheFrameThreadRunsInline) {
    LinuxUIScheduler scheduler;
    int runs = 0;

    scheduler.scheduleOnUI([&runs] { ++runs; });

    EXPECT_EQ(runs, 1);
}

/** ADR-0003: a job from another thread waits for the frame's drain, runs there once, and never again. */
TEST(LinuxUISchedulerTest, AJobFromAnotherThreadRunsOnceOnTheFrameThreadAtTheDrain) {
    LinuxUIScheduler scheduler;
    std::vector<std::thread::id> runThreads;

    std::thread([&scheduler, &runThreads] {
        scheduler.scheduleOnUI([&runThreads] { runThreads.push_back(std::this_thread::get_id()); });
    }).join();

    EXPECT_TRUE(runThreads.empty());

    scheduler.triggerUI();
    scheduler.triggerUI();

    EXPECT_EQ(runThreads, std::vector<std::thread::id>{std::this_thread::get_id()});
}

TEST(LinuxUISchedulerTest, JobsStillQueuedAtTeardownAreDestroyedWithoutRunning) {
    auto scheduler = std::make_unique<LinuxUIScheduler>();
    const auto capture = std::make_shared<int>(0);

    std::thread([&scheduler, capture] { scheduler->scheduleOnUI([capture] { ++*capture; }); }).join();
    scheduler.reset();

    EXPECT_EQ(*capture, 0);
    EXPECT_EQ(capture.use_count(), 1);
}

/**
 * The TSan half of #135: jobs pushed from several threads while the frame thread drains. The counter is a plain
 * int touched only by the jobs, so a job that ran anywhere but the frame thread, or a push that did not
 * happen-before its pop, is a data race the sanitizer reports.
 */
TEST(LinuxUISchedulerTest, JobsFromManyThreadsAllRunOnTheFrameThreadWhileItDrains) {
    LinuxUIScheduler scheduler;
    int runs = 0;
    std::vector<std::thread> schedulingThreads;

    schedulingThreads.reserve(kSchedulingThreadCount);

    for (int thread = 0; thread < kSchedulingThreadCount; ++thread) {
        schedulingThreads.emplace_back([&scheduler, &runs] {
            for (int job = 0; job < kJobsPerThread; ++job) {
                scheduler.scheduleOnUI([&runs] { ++runs; });
            }
        });
    }

    while (runs < kSchedulingThreadCount * kJobsPerThread) {
        scheduler.triggerUI();
        std::this_thread::yield();
    }

    for (std::thread& schedulingThread : schedulingThreads) {
        schedulingThread.join();
    }

    scheduler.triggerUI();

    EXPECT_EQ(runs, kSchedulingThreadCount * kJobsPerThread);
}

} // namespace
} // namespace react_native_linux
