#pragma once

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <unordered_map>

#include <react/runtime/PlatformTimerRegistry.h>
#include <react/runtime/TimerManager.h>
#include <react/threading/TaskDispatchThread.h>

namespace react_native_linux {

/**
 * Platform timer registry backed by a single dispatch thread.
 *
 * Threading contract: createTimer, createRecurringTimer and deleteTimer are called on the JS thread by
 * TimerManager; callTimer is invoked from the dispatch thread and hands the callback back to the JS thread through
 * the runtime executor. hasPendingTimers and waitUntilIdle are called from the thread that owns the process run
 * loop, which is the main thread for the hello_react host.
 *
 * Destruction order: taskDispatchThread_ is declared last so it is destroyed first, joining the dispatch thread
 * before timersMutex_ and idleCondition_ are torn down. Any other order lets the dispatch thread call
 * idleCondition_.notify_all() on an already-destroyed condition variable.
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

    /**
     * Fantom's timer mock (#210), with upstream `FantomTimerRegistry`'s semantics. While it is enabled, a new timer
     * waits on a virtual clock instead of the dispatch thread and fires only when `advanceTimersByTime` or
     * `runAllTimers` reaches it: earliest due first, then the one created first, a recurring one re-armed each time
     * it fires. Disabling it drops the virtual timers, which nothing could fire any more. Called on the JavaScript
     * thread; `hasPendingTimers` and `waitUntilIdle` see only the real timers, so a mock left installed never holds
     * a run open.
     */
    void setMockEnabled(bool enabled);
    void advanceTimersByTime(double deltaMilliseconds);
    void runAllTimers();
    size_t pendingMockTimerCount();

private:
    struct ScheduledTimer {
        double delayMilliseconds;
        bool isRecurring;
    };

    struct MockTimer {
        double dueMilliseconds;
        double intervalMilliseconds;
        bool isRecurring;
    };

    void scheduleTimer(uint32_t timerId, double delayMilliseconds, bool isRecurring);
    void dispatchTimer(uint32_t timerId, double delayMilliseconds);

    /** Fires the earliest virtual timer due by `dueByMilliseconds`, or any when there is no bound; false if none. */
    bool fireNextMockTimer(std::optional<double> dueByMilliseconds);

    std::weak_ptr<facebook::react::TimerManager> timerManager_;
    std::mutex timersMutex_;
    std::condition_variable idleCondition_;
    std::unordered_map<uint32_t, ScheduledTimer> timers_;
    bool isMockEnabled_{false};
    double mockNowMilliseconds_{0.0};
    std::map<uint32_t, MockTimer> mockTimers_;
    facebook::react::TaskDispatchThread taskDispatchThread_{"TimerRegistry"};
};

} // namespace react_native_linux
