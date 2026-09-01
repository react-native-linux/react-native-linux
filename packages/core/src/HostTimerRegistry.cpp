#include "HostTimerRegistry.h"

#include <chrono>
#include <memory>
#include <mutex>
#include <utility>

namespace react_native_linux {

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

void HostTimerRegistry::scheduleTimer(uint32_t timerId, double delayMilliseconds, bool isRecurring) {
    {
        const std::lock_guard<std::mutex> guard(timersMutex_);
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
