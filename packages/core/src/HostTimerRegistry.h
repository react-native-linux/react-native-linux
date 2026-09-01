#pragma once

#include <react/runtime/PlatformTimerRegistry.h>
#include <react/runtime/TimerManager.h>
#include <react/threading/TaskDispatchThread.h>

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <unordered_map>

namespace react_native_linux {

/**
 * Platform timer registry backed by a single dispatch thread.
 *
 * Threading contract: createTimer, createRecurringTimer and deleteTimer are called on the JS thread by
 * TimerManager; callTimer is invoked from the dispatch thread and hands the callback back to the JS thread through
 * the runtime executor. hasPendingTimers and waitUntilIdle are called from the thread that owns the process run
 * loop, which is the main thread for the hello_react host.
 */
class HostTimerRegistry final : public facebook::react::PlatformTimerRegistry {
public:
    HostTimerRegistry() noexcept = default;
    HostTimerRegistry(const HostTimerRegistry&) = delete;
    HostTimerRegistry(HostTimerRegistry&&) = delete;
    HostTimerRegistry& operator=(const HostTimerRegistry&) = delete;
    HostTimerRegistry& operator=(HostTimerRegistry&&) = delete;
    ~HostTimerRegistry() noexcept override = default;

    void createTimer(uint32_t timerId, double delayMilliseconds) override;
    void deleteTimer(uint32_t timerId) override;
    void createRecurringTimer(uint32_t timerId, double delayMilliseconds) override;
    void setTimerManager(std::weak_ptr<facebook::react::TimerManager> timerManager) override;
    void quit() override;

    bool hasPendingTimers();
    bool waitUntilIdle(std::chrono::milliseconds timeout);

private:
    struct ScheduledTimer {
        double delayMilliseconds;
        bool isRecurring;
    };

    void scheduleTimer(uint32_t timerId, double delayMilliseconds, bool isRecurring);
    void dispatchTimer(uint32_t timerId, double delayMilliseconds);

    facebook::react::TaskDispatchThread taskDispatchThread_{"TimerRegistry"};
    std::weak_ptr<facebook::react::TimerManager> timerManager_;
    std::mutex timersMutex_;
    std::condition_variable idleCondition_;
    std::unordered_map<uint32_t, ScheduledTimer> timers_;
};

} // namespace react_native_linux
