#pragma once

#include <functional>
#include <thread>
#include <worklets/Tools/UIScheduler.h>

namespace react_native_linux {

/**
 * react-native-worklets' UI thread on Linux is the frame thread (ADR-0003, #135): the thread that constructs this
 * scheduler, which in a window is the run loop's thread that paints and presents.
 *
 * Threading contract: `scheduleOnUI` may be called from any thread. On the frame thread it runs the job inline, as
 * `IOSUIScheduler` does on the main thread; from any other thread it queues the job, and the frame thread runs
 * every queued job, once each, when it calls upstream's `triggerUI` at its one point in the frame. Jobs still
 * queued when the scheduler is destroyed are destroyed without running. `hasPendingJobs` may be read from any thread.
 */
class LinuxUIScheduler final : public worklets::UIScheduler {
public:
    void scheduleOnUI(std::function<void()> job) override;

    /** Whether a job is queued for the next `triggerUI`, for the frame clock's pending-work signal. */
    bool hasPendingJobs() const;

protected:
    bool queryIsOnUIThread() const override;

private:
    const std::thread::id frameThread_{std::this_thread::get_id()};
};

} // namespace react_native_linux
