#pragma once

#include "HermesJSRuntimeFactory.h"
#include "HostTimerRegistry.h"
#include "JsErrorReporter.h"

#include <cxxreact/JSBigString.h>
#include <react/runtime/ReactInstance.h>
#include <react/runtime/TimerManager.h>
#include <react/threading/MessageQueueThreadImpl.h>

#include <chrono>
#include <memory>
#include <string>

namespace react_native_linux {

/**
 * One bridgeless `ReactInstance` and the threads it needs: a `MessageQueueThreadImpl` as the JavaScript thread, a
 * `TimerManager` over a dispatch-thread-backed `PlatformTimerRegistry`, and the error handler that prints
 * structured JavaScript errors. Everything a host needs before Fabric exists, and nothing a host has to decide
 * about how long to keep running.
 *
 * Both hosts build on this: `BundleRunner` runs to quiescence and exits, and `WindowSession` keeps the instance
 * alive for as long as the window is open.
 *
 * Threading contract: every member here is called from the thread that constructed the host — the process run
 * loop for the headless host, the platform frame thread for the window host. The JavaScript that the instance
 * runs never touches this object; it runs on the JavaScript thread this class owns.
 *
 * Shutdown contract: destruction quits the JavaScript thread synchronously and only then destroys the instance,
 * because the instance's teardown assumes no further task can be scheduled onto that thread. Anything layered on
 * top — a Fabric surface, for example — must already have been stopped and drained by its owner.
 */
class ReactHost final {
public:
    ReactHost();
    ReactHost(const ReactHost&) = delete;
    ReactHost(ReactHost&&) = delete;
    ReactHost& operator=(const ReactHost&) = delete;
    ReactHost& operator=(ReactHost&&) = delete;
    ~ReactHost() noexcept;

    facebook::react::ReactInstance& reactInstance() noexcept;
    void loadScript(std::unique_ptr<const facebook::react::JSBigString> script, const std::string& sourceUrl);
    void drainJavaScriptThread();
    bool runUntilQuiescent(std::chrono::milliseconds budget);
    bool hasReportedFatalError() const;

    /**
     * Whether a JS timer is outstanding, for the frame clock's fallback-timeout pending-work signal (see
     * *Frame clock* in docs/cpp-toolchain.md). This is "any timer scheduled", not "a timer due before the next
     * tick" — `HostTimerRegistry` tracks delay and recurrence, not an absolute deadline — so it is a conservative
     * signal: it can hold the fallback awake slightly ahead of when a distant timer actually fires, never behind.
     */
    bool hasPendingTimers() const;

private:
    std::shared_ptr<facebook::react::MessageQueueThreadImpl> javaScriptThread_;
    HostTimerRegistry* timerRegistry_{nullptr};
    std::shared_ptr<facebook::react::TimerManager> timerManager_;
    JsErrorReporter errorReporter_;
    HermesJSRuntimeFactory runtimeFactory_;
    std::unique_ptr<facebook::react::ReactInstance> reactInstance_;
};

} // namespace react_native_linux
