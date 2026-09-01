#include "ReactHost.h"

#include "ConsoleBinding.h"

#include <react/featureflags/ReactNativeFeatureFlags.h>
#include <react/featureflags/ReactNativeFeatureFlagsOverridesOSSStable.h>

#include <chrono>
#include <memory>
#include <string>
#include <utility>

namespace react_native_linux {

namespace {

std::chrono::milliseconds remainingBudget(std::chrono::steady_clock::time_point deadline) {
    return std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now());
}

} // namespace

ReactHost::ReactHost() : javaScriptThread_(std::make_shared<facebook::react::MessageQueueThreadImpl>()) {
    facebook::react::ReactNativeFeatureFlags::override(
        std::make_unique<facebook::react::ReactNativeFeatureFlagsOverridesOSSStable>());

    std::unique_ptr<HostTimerRegistry> ownedTimerRegistry = std::make_unique<HostTimerRegistry>();
    timerRegistry_ = ownedTimerRegistry.get();
    timerManager_ = std::make_shared<facebook::react::TimerManager>(std::move(ownedTimerRegistry));
    timerRegistry_->setTimerManager(timerManager_);

    reactInstance_ = std::make_unique<facebook::react::ReactInstance>(
        runtimeFactory_.createJSRuntime(javaScriptThread_), javaScriptThread_, timerManager_,
        errorReporter_.createHandler());
    timerManager_->setRuntimeExecutor(reactInstance_->getBufferedRuntimeExecutor());

    reactInstance_->initializeRuntime({}, installConsoleBinding);
}

ReactHost::~ReactHost() noexcept {
    javaScriptThread_->quitSynchronous();
    reactInstance_.reset();
}

facebook::react::ReactInstance& ReactHost::reactInstance() noexcept { return *reactInstance_; }

void ReactHost::loadScript(std::unique_ptr<const facebook::react::JSBigString> script, const std::string& sourceUrl) {
    reactInstance_->loadScript(std::move(script), sourceUrl);
}

void ReactHost::drainJavaScriptThread() {
    javaScriptThread_->runOnQueueSync([]() {});
}

bool ReactHost::runUntilQuiescent(std::chrono::milliseconds budget) {
    const std::chrono::steady_clock::time_point deadline = std::chrono::steady_clock::now() + budget;

    while (true) {
        drainJavaScriptThread();

        if (!timerRegistry_->hasPendingTimers()) {
            return true;
        }

        if (!timerRegistry_->waitUntilIdle(remainingBudget(deadline))) {
            return false;
        }
    }
}

bool ReactHost::hasReportedFatalError() const { return errorReporter_.hasReportedFatalError(); }

} // namespace react_native_linux
