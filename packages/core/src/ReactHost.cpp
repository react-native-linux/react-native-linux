#include "ReactHost.h"

#include "ConsoleBinding.h"
#include "ReactNativeFeatureFlagsOverridesLinux.h"

#include <atomic>
#include <chrono>
#include <cstddef>
#include <jsi/jsi.h>
#include <memory>
#include <react/featureflags/ReactNativeFeatureFlags.h>
#include <react/renderer/animated/NativeAnimatedNodesManagerProvider.h>
#include <react/renderer/runtimescheduler/RuntimeSchedulerCallInvoker.h>
#include <string>
#include <thread>
#include <utility>

namespace react_native_linux {

namespace {

std::chrono::milliseconds remainingBudget(std::chrono::steady_clock::time_point deadline) {
    return std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now());
}

// #214's MarkTestPassed, and the whole of #236's protocol: a bundle that has asserted for itself says so, and the
// automation channel reports it. Nothing reads the flag from JavaScript, so there is no getter to install.
void installMarkTestPassedBinding(facebook::jsi::Runtime& runtime,
                                  const std::shared_ptr<std::atomic<bool>>& hasMarkedTestPassed) {
    runtime.global().setProperty(
        runtime, "__rnlMarkTestPassed",
        facebook::jsi::Function::createFromHostFunction(
            runtime, facebook::jsi::PropNameID::forAscii(runtime, "__rnlMarkTestPassed"), 0,
            [hasMarkedTestPassed](facebook::jsi::Runtime& /*hostRuntime*/, const facebook::jsi::Value& /*thisValue*/,
                                  const facebook::jsi::Value* /*arguments*/, size_t /*argumentCount*/) {
                hasMarkedTestPassed->store(true);

                return facebook::jsi::Value::undefined();
            }));
}

} // namespace

ReactHost::ReactHost() : javaScriptThread_(std::make_shared<facebook::react::MessageQueueThreadImpl>()) {
    facebook::react::ReactNativeFeatureFlags::override(std::make_unique<ReactNativeFeatureFlagsOverridesLinux>());

    std::unique_ptr<HostTimerRegistry> ownedTimerRegistry = std::make_unique<HostTimerRegistry>();
    timerRegistry_ = ownedTimerRegistry.get();
    timerManager_ = std::make_shared<facebook::react::TimerManager>(std::move(ownedTimerRegistry));
    timerRegistry_->setTimerManager(timerManager_);

    reactInstance_ = std::make_unique<facebook::react::ReactInstance>(
        runtimeFactory_.createJSRuntime(javaScriptThread_), javaScriptThread_, timerManager_,
        errorReporter_.createHandler());
    timerManager_->setRuntimeExecutor(reactInstance_->getBufferedRuntimeExecutor());

    animatedNodesManagerProvider_ = std::make_shared<facebook::react::NativeAnimatedNodesManagerProvider>();
    turboModuleRegistry_ = std::make_unique<TurboModuleRegistry>(
        std::make_shared<facebook::react::RuntimeSchedulerCallInvoker>(reactInstance_->getRuntimeScheduler()),
        animatedNodesManagerProvider_);

    reactInstance_->initializeRuntime({},
                                      [registry = turboModuleRegistry_.get(),
                                       hasMarkedTestPassed = hasMarkedTestPassed_](facebook::jsi::Runtime& runtime) {
                                          installConsoleBinding(runtime);
                                          installMarkTestPassedBinding(runtime, hasMarkedTestPassed);
                                          registry->install(runtime);
                                      });
}

ReactHost::~ReactHost() noexcept {
    javaScriptThread_->quitSynchronous();
    turboModuleRegistry_.reset();
    animatedNodesManagerProvider_.reset();
    reactInstance_.reset();
}

facebook::react::ReactInstance& ReactHost::reactInstance() noexcept { return *reactInstance_; }

DimensionsSource& ReactHost::dimensions() noexcept { return turboModuleRegistry_->dimensions(); }

void ReactHost::publishPendingDimensions() { turboModuleRegistry_->publishPendingDimensions(); }

void ReactHost::loadScript(std::unique_ptr<const facebook::react::JSBigString> script, const std::string& sourceUrl) {
    reactInstance_->loadScript(std::move(script), sourceUrl);
}

void ReactHost::drainJavaScriptThread() {
    javaScriptThread_->runOnQueueSync([]() {});
}

void ReactHost::blockJavaScriptThread(std::chrono::milliseconds duration) {
    javaScriptThread_->runOnQueueSync([duration]() { std::this_thread::sleep_for(duration); });
}

bool ReactHost::hasMarkedTestPassed() const { return hasMarkedTestPassed_->load(); }

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

bool ReactHost::hasPendingTimers() const { return timerRegistry_->hasPendingTimers(); }

} // namespace react_native_linux
