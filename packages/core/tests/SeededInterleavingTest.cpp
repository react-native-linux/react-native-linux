#include "HostTimerRegistry.h"
#include "SeededTestScheduler.h"

#include <chrono>
#include <cstdint>
#include <gtest/gtest.h>
#include <vector>

namespace react_native_linux {

namespace {

constexpr std::uint32_t kTimerCount = 4;
constexpr std::uint64_t kLongestDelayMilliseconds = 10;
constexpr std::uint64_t kRecurringChoiceBound = 2;
constexpr std::uint64_t kRecurringChoice = 1;

/**
 * #212's timer-seam contract — a deleted timer never fires and the registry always reaches idle — re-run under
 * a seeded order of its operations rather than under the single hand-written order the original test asserts.
 * The seed decides which timers are recurring, how long each delay is, the order the creations are issued in,
 * and the order the deletions are issued in; the contract is supposed to hold for every one of those orders,
 * which is exactly what a hundred seeds check.
 */
bool theTimerSeamSettlesUnderThisInterleaving(SeededTestScheduler& scheduler) {
    HostTimerRegistry registry;

    for (std::uint32_t timerId = 1; timerId <= kTimerCount; ++timerId) {
        const bool isRecurring = scheduler.nextChoice(kRecurringChoiceBound) == kRecurringChoice;
        const auto delayMilliseconds = static_cast<double>(scheduler.nextChoice(kLongestDelayMilliseconds) + 1);

        scheduler.schedule([&registry, timerId, isRecurring, delayMilliseconds] {
            if (isRecurring) {
                registry.createRecurringTimer(timerId, delayMilliseconds);
            } else {
                registry.createTimer(timerId, delayMilliseconds);
            }
        });
    }

    if (scheduler.run().has_value()) {
        return false;
    }

    for (std::uint32_t timerId = 1; timerId <= kTimerCount; ++timerId) {
        scheduler.schedule([&registry, timerId] { registry.deleteTimer(timerId); });
    }

    if (scheduler.run().has_value()) {
        return false;
    }

    return registry.waitUntilIdle(std::chrono::milliseconds(5000)) && !registry.hasPendingTimers();
}

} // namespace

TEST(SeededInterleavingTest, TheTimerSeamSettlesUnderEverySeededOrderOfCreationAndCancellation) {
    const std::optional<SeededIterationFailure> failure =
        runSeededIterations("SeededInterleavingTest", theTimerSeamSettlesUnderThisInterleaving);

    ASSERT_FALSE(failure.has_value()) << failure->replayInstruction;
}

} // namespace react_native_linux
