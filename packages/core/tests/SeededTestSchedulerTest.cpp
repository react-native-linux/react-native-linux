#include "SeededTestScheduler.h"

#include <chrono>
#include <cstdlib>
#include <gtest/gtest.h>
#include <string>
#include <vector>

namespace react_native_linux {

namespace {

/** Sets `SEED` and `ITERATIONS` for one test and restores whatever the surrounding process had. */
class ScopedEnvironment final {
public:
    /** A null `value` unsets the variable for the duration of the test. */
    ScopedEnvironment(const char* variableName, const char* value) : variableName_(variableName) {
        const char* const previous = std::getenv(variableName);

        if (previous != nullptr) {
            previousValue_ = previous;
            hadValue_ = true;
        }

        if (value == nullptr) {
            unsetenv(variableName);
        } else {
            setenv(variableName, value, 1);
        }
    }

    ScopedEnvironment(const ScopedEnvironment&) = delete;
    ScopedEnvironment(ScopedEnvironment&&) = delete;
    ScopedEnvironment& operator=(const ScopedEnvironment&) = delete;
    ScopedEnvironment& operator=(ScopedEnvironment&&) = delete;

    ~ScopedEnvironment() {
        if (hadValue_) {
            setenv(variableName_, previousValue_.c_str(), 1);
        } else {
            unsetenv(variableName_);
        }
    }

private:
    const char* variableName_;
    std::string previousValue_;
    bool hadValue_{false};
};

std::vector<int> executionOrderForSeed(std::uint64_t seed, int taskCount) {
    SeededTestScheduler scheduler(seed);
    std::vector<int> order;

    for (int task = 0; task < taskCount; ++task) {
        scheduler.schedule([&order, task] { order.push_back(task); });
    }

    EXPECT_EQ(scheduler.run(), std::nullopt);

    return order;
}

constexpr int kInterleavedTaskCount = 6;

} // namespace

TEST(SeededTestSchedulerTest, OneSeedReproducesOneInterleavingExactly) {
    EXPECT_EQ(executionOrderForSeed(7, kInterleavedTaskCount), executionOrderForSeed(7, kInterleavedTaskCount));
}

TEST(SeededTestSchedulerTest, TheReadyOrderIsRandomisedSoTwoSeedsDiffer) {
    const std::vector<int> firstOrder = executionOrderForSeed(1, kInterleavedTaskCount);
    bool anySeedDiffers = false;

    for (std::uint64_t seed = 2; seed < 12; ++seed) {
        anySeedDiffers = anySeedDiffers || executionOrderForSeed(seed, kInterleavedTaskCount) != firstOrder;
    }

    EXPECT_TRUE(anySeedDiffers);
    EXPECT_EQ(firstOrder.size(), static_cast<size_t>(kInterleavedTaskCount));
}

TEST(SeededTestSchedulerTest, TheVirtualClockJumpsToTheNextTimerInsteadOfSleeping) {
    SeededTestScheduler scheduler(3);
    std::vector<int> firedDelays;

    const auto startedAt = std::chrono::steady_clock::now();

    scheduler.scheduleAfter(std::chrono::milliseconds(5000), [&firedDelays] { firedDelays.push_back(5000); });
    scheduler.scheduleAfter(std::chrono::milliseconds(1000), [&firedDelays] { firedDelays.push_back(1000); });

    EXPECT_EQ(scheduler.run(), std::nullopt);

    const auto elapsed = std::chrono::steady_clock::now() - startedAt;

    EXPECT_LT(elapsed, std::chrono::seconds(1));
    EXPECT_EQ(firedDelays, (std::vector<int>{1000, 5000}));
    EXPECT_EQ(scheduler.now(), std::chrono::milliseconds(5000));
}

TEST(SeededTestSchedulerTest, ATimerScheduledFromInsideATaskIsRelativeToTheVirtualClock) {
    SeededTestScheduler scheduler(4);
    std::vector<std::chrono::milliseconds> observedTimes;

    scheduler.scheduleAfter(std::chrono::milliseconds(100), [&scheduler, &observedTimes] {
        observedTimes.push_back(scheduler.now());
        scheduler.scheduleAfter(std::chrono::milliseconds(50),
                                [&scheduler, &observedTimes] { observedTimes.push_back(scheduler.now()); });
    });

    EXPECT_EQ(scheduler.run(), std::nullopt);
    EXPECT_EQ(observedTimes,
              (std::vector<std::chrono::milliseconds>{std::chrono::milliseconds(100), std::chrono::milliseconds(150)}));
}

TEST(SeededTestSchedulerTest, ATimeoutResolvesAfterASeededTickCountRatherThanAWallClockDelay) {
    SeededTestScheduler scheduler(11);
    std::uint64_t tickAtTimeout = 0;
    bool timedOut = false;

    const std::uint64_t chosenTicks = scheduler.scheduleAfterSeededTicks(8, [&scheduler, &tickAtTimeout, &timedOut] {
        tickAtTimeout = scheduler.tickCount();
        timedOut = true;
    });

    for (int task = 0; task < 8; ++task) {
        scheduler.schedule([] {});
    }

    EXPECT_GE(chosenTicks, 1U);
    EXPECT_LE(chosenTicks, 8U);
    EXPECT_EQ(scheduler.run(), std::nullopt);
    EXPECT_TRUE(timedOut);
    EXPECT_GE(tickAtTimeout, chosenTicks);
    EXPECT_EQ(SeededTestScheduler(11).scheduleAfterSeededTicks(8, [] {}), chosenTicks);
}

TEST(SeededTestSchedulerTest, TheTickCountATimeoutWaitsForIsChosenBySeedRatherThanFixed) {
    std::vector<std::uint64_t> chosenTicksPerSeed;

    for (std::uint64_t seed = 0; seed < 12; ++seed) {
        SeededTestScheduler scheduler(seed);
        chosenTicksPerSeed.push_back(scheduler.scheduleAfterSeededTicks(8, [] {}));
    }

    EXPECT_NE(chosenTicksPerSeed, std::vector<std::uint64_t>(chosenTicksPerSeed.size(), chosenTicksPerSeed.front()));
}

TEST(SeededTestSchedulerTest, ATimeoutWithNoOtherWorkJumpsTheTickCounterInsteadOfNeverFiring) {
    SeededTestScheduler scheduler(5);
    int firedCount = 0;

    EXPECT_EQ(scheduler.scheduleAfterSeededTicks(1, [&firedCount] { ++firedCount; }), 1U);
    EXPECT_EQ(scheduler.scheduleAfterSeededTicks(1, [&firedCount] { ++firedCount; }), 1U);
    EXPECT_EQ(scheduler.run(), std::nullopt);
    EXPECT_EQ(firedCount, 2);
    EXPECT_EQ(scheduler.tickCount(), 3U);
}

TEST(SeededTestSchedulerTest, ParkingIsRefusedAndTheReportNamesEveryUnfinishedWait) {
    SeededTestScheduler scheduler(9);

    const std::uint64_t completed = scheduler.beginWait("mount-transaction");
    scheduler.beginWait("promise-resolution");
    scheduler.beginWait("frame-callback");

    scheduler.schedule([&scheduler, completed] { scheduler.completeWait(completed); });

    const std::optional<std::string> report = scheduler.run();

    ASSERT_TRUE(report.has_value());
    EXPECT_EQ(
        *report,
        "forbidden parking: seed 9 drained after 1 ticks with unfinished waits: promise-resolution frame-callback");
}

TEST(SeededTestSchedulerTest, CompletingEveryWaitDrainsWithoutAParkingReport) {
    SeededTestScheduler scheduler(9);

    const std::uint64_t wait = scheduler.beginWait("mount-transaction");

    scheduler.schedule([&scheduler, wait] { scheduler.completeWait(wait); });
    scheduler.completeWait(wait + 1000);

    EXPECT_EQ(scheduler.run(), std::nullopt);
}

TEST(SeededTestSchedulerTest, AChoiceWithoutABoundIsZeroAndDoesNotConsumeTheSeededSource) {
    SeededTestScheduler scheduler(2);

    EXPECT_EQ(scheduler.nextChoice(0), 0U);
    EXPECT_EQ(scheduler.nextChoice(4), SeededTestScheduler(2).nextChoice(4));
    EXPECT_EQ(scheduler.seed(), 2U);
}

TEST(SeededTestSchedulerTest, TheSeedAndTheIterationCountComeFromTheEnvironment) {
    const ScopedEnvironment seed("SEED", "4242");
    const ScopedEnvironment iterations("ITERATIONS", "7");

    EXPECT_EQ(seedFromEnvironment(0), 4242U);
    EXPECT_EQ(iterationsFromEnvironment(100), 7U);
}

TEST(SeededTestSchedulerTest, AnUnparsableEnvironmentValueFallsBackToTheDefault) {
    {
        const ScopedEnvironment unparsableSeed("SEED", "not-a-number");
        EXPECT_EQ(seedFromEnvironment(13), 13U);
    }

    const ScopedEnvironment iterations("ITERATIONS", "7x");
    EXPECT_EQ(iterationsFromEnvironment(21), 21U);
}

TEST(SeededTestSchedulerTest, AnUnsetEnvironmentValueFallsBackToTheDefault) {
    const ScopedEnvironment seed("SEED", nullptr);
    const ScopedEnvironment iterations("ITERATIONS", nullptr);

    EXPECT_EQ(seedFromEnvironment(17), 17U);
    EXPECT_EQ(iterationsFromEnvironment(19), 19U);
}

TEST(SeededTestSchedulerTest, AFailingIterationReportsItsSeedAndTheCommandThatReplaysIt) {
    const ScopedEnvironment seed("SEED", "1000");
    const ScopedEnvironment iterations("ITERATIONS", "50");

    const std::optional<SeededIterationFailure> failure = runSeededIterations(
        "SeededTestSchedulerTest", [](SeededTestScheduler& scheduler) { return scheduler.seed() != 1003; });

    ASSERT_TRUE(failure.has_value());
    EXPECT_EQ(failure->seed, 1003U);
    EXPECT_EQ(
        failure->replayInstruction,
        "failing seed: 1003 — replay with: SEED=1003 ITERATIONS=1 ctest --preset test -R SeededTestSchedulerTest");
}

TEST(SeededTestSchedulerTest, EveryIterationRunsItsOwnSeedAndAPassingBodyReportsNoFailure) {
    const ScopedEnvironment seed("SEED", "500");
    const ScopedEnvironment iterations("ITERATIONS", "4");

    std::vector<std::uint64_t> seedsSeen;

    const std::optional<SeededIterationFailure> failure =
        runSeededIterations("SeededTestSchedulerTest", [&seedsSeen](SeededTestScheduler& scheduler) {
            seedsSeen.push_back(scheduler.seed());
            return true;
        });

    EXPECT_FALSE(failure.has_value());
    EXPECT_EQ(seedsSeen, (std::vector<std::uint64_t>{500, 501, 502, 503}));
}

} // namespace react_native_linux
