#include "HostTimerRegistry.h"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <memory>
#include <mutex>
#include <optional>
#include <utility>

namespace react_native_linux {

namespace {

// Upstream `FantomTimerRegistry`'s bound, so a zero-interval `setInterval` cannot spin one advance forever.
constexpr size_t kMaximumMockTimerFires = 100000;

} // namespace

void HostTimerRegistry::createTimer(uint32_t timerId, double delayMilliseconds) {
    scheduleTimer(timerId, delayMilliseconds, false);
}

void HostTimerRegistry::createRecurringTimer(uint32_t timerId, double delayMilliseconds) {
    scheduleTimer(timerId, delayMilliseconds, true);
}

void HostTimerRegistry::deleteTimer(uint32_t timerId) {
    {
        const std::lock_guard<std::mutex> guard(timersMutex_);
        timers_.erase(timerId);
        mockTimers_.erase(timerId);
    }

    idleCondition_.notify_all();
}

void HostTimerRegistry::setTimerManager(std::weak_ptr<facebook::react::TimerManager> timerManager) {
    timerManager_ = std::move(timerManager);
}

void HostTimerRegistry::quit() {
    taskDispatchThread_.quit();

    {
        const std::lock_guard<std::mutex> guard(timersMutex_);
        timers_.clear();
        mockTimers_.clear();
    }

    idleCondition_.notify_all();
}

bool HostTimerRegistry::hasPendingTimers() {
    const std::lock_guard<std::mutex> guard(timersMutex_);

    return !timers_.empty();
}

bool HostTimerRegistry::waitUntilIdle(std::chrono::milliseconds timeout) {
    std::unique_lock<std::mutex> lock(timersMutex_);

    return idleCondition_.wait_for(lock, timeout, [this]() { return timers_.empty(); });
}

void HostTimerRegistry::setMockEnabled(bool enabled) {
    const std::lock_guard<std::mutex> guard(timersMutex_);

    isMockEnabled_ = enabled;

    if (!enabled) {
        mockTimers_.clear();
    }
}

void HostTimerRegistry::advanceTimersByTime(double deltaMilliseconds) {
    double targetMilliseconds = 0.0;

    {
        const std::lock_guard<std::mutex> guard(timersMutex_);

        targetMilliseconds = mockNowMilliseconds_ + std::max(deltaMilliseconds, 0.0);
    }

    size_t firedCount = 0;

    while (firedCount < kMaximumMockTimerFires && fireNextMockTimer(targetMilliseconds)) {
        ++firedCount;
    }

    const std::lock_guard<std::mutex> guard(timersMutex_);

    mockNowMilliseconds_ = targetMilliseconds;
}

void HostTimerRegistry::runAllTimers() {
    size_t firedCount = 0;

    while (firedCount < kMaximumMockTimerFires && fireNextMockTimer(std::nullopt)) {
        ++firedCount;
    }
}

size_t HostTimerRegistry::pendingMockTimerCount() {
    const std::lock_guard<std::mutex> guard(timersMutex_);

    return mockTimers_.size();
}

bool HostTimerRegistry::fireNextMockTimer(std::optional<double> dueByMilliseconds) {
    uint32_t timerId = 0;

    {
        const std::lock_guard<std::mutex> guard(timersMutex_);
        auto next = mockTimers_.end();

        for (auto timer = mockTimers_.begin(); timer != mockTimers_.end(); ++timer) {
            const bool isDue = !dueByMilliseconds.has_value() || timer->second.dueMilliseconds <= *dueByMilliseconds;

            if (isDue && (next == mockTimers_.end() || timer->second.dueMilliseconds < next->second.dueMilliseconds)) {
                next = timer;
            }
        }

        if (next == mockTimers_.end()) {
            return false;
        }

        timerId = next->first;
        mockNowMilliseconds_ = next->second.dueMilliseconds;

        if (next->second.isRecurring) {
            next->second.dueMilliseconds += next->second.intervalMilliseconds;
        } else {
            mockTimers_.erase(next);
        }
    }

    if (const std::shared_ptr<facebook::react::TimerManager> timerManager = timerManager_.lock()) {
        timerManager->callTimer(static_cast<facebook::react::TimerHandle>(timerId));
    }

    return true;
}

void HostTimerRegistry::scheduleTimer(uint32_t timerId, double delayMilliseconds, bool isRecurring) {
    {
        const std::lock_guard<std::mutex> guard(timersMutex_);

        if (isMockEnabled_) {
            const double intervalMilliseconds = std::max(delayMilliseconds, 0.0);

            mockTimers_.insert_or_assign(timerId,
                                         MockTimer{.dueMilliseconds = mockNowMilliseconds_ + intervalMilliseconds,
                                                   .intervalMilliseconds = intervalMilliseconds,
                                                   .isRecurring = isRecurring});

            return;
        }

        timers_.insert_or_assign(timerId,
                                 ScheduledTimer{.delayMilliseconds = delayMilliseconds, .isRecurring = isRecurring});
    }

    dispatchTimer(timerId, delayMilliseconds);
}

void HostTimerRegistry::dispatchTimer(uint32_t timerId, double delayMilliseconds) {
    taskDispatchThread_.runAsync(
        [this, timerId, delayMilliseconds]() {
            bool isRecurring = false;

            {
                const std::lock_guard<std::mutex> guard(timersMutex_);
                const auto scheduledTimer = timers_.find(timerId);

                if (scheduledTimer == timers_.end()) {
                    return;
                }

                isRecurring = scheduledTimer->second.isRecurring;
            }

            if (const std::shared_ptr<facebook::react::TimerManager> timerManager = timerManager_.lock()) {
                timerManager->callTimer(static_cast<facebook::react::TimerHandle>(timerId));
            }

            if (isRecurring) {
                dispatchTimer(timerId, delayMilliseconds);
                return;
            }

            {
                const std::lock_guard<std::mutex> guard(timersMutex_);
                timers_.erase(timerId);
            }

            idleCondition_.notify_all();
        },
        std::chrono::milliseconds(static_cast<int64_t>(delayMilliseconds)));
}

} // namespace react_native_linux
