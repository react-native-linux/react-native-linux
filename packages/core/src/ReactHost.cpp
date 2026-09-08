#include "ReactHost.h"

#include "ConsoleBinding.h"
#include "ReactNativeFeatureFlagsOverridesLinux.h"

#include <cmath>
#include <jsi/jsi.h>
#include <react/featureflags/ReactNativeFeatureFlags.h>
#include <react/renderer/animated/NativeAnimatedNodesManagerProvider.h>
#include <react/renderer/runtimescheduler/RuntimeSchedulerCallInvoker.h>

#include <atomic>
#include <chrono>
#include <cstddef>
#include <memory>
#include <string>
#include <thread>
#include <utility>

namespace react_native_linux {

namespace {

std::chrono::milliseconds remainingBudget(std::chrono::steady_clock::time_point deadline) {
    return std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now());
}

// One past the largest `AnimationFrameQueue` handle a `double` can hold exactly enough of to convert: 2^64.
constexpr double kHandleUpperBound = 0x1p64;

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

/**
 * Replaces the `requestAnimationFrame` and `cancelAnimationFrame` that `TimerManager::attachGlobals` installs.
 *
 * `ReactInstance::initializeRuntime` runs the bindings installer after `attachGlobals`, so assigning the two
 * properties here is what decides which implementation JavaScript sees. Upstream's is `setTimeout(callback, 0)`
 * by its own admission, which on this platform means a `TaskDispatchThread` heap ordered by deadline alone and no
 * relationship to a frame at all; `AnimationFrameQueue` is the frame-coupled one. Everything about *when* a
 * callback runs lives in that queue — this function only moves values across the JSI boundary.
 *
 * The callback is held in a `shared_ptr` because `jsi::Function` is move-only and `std::function` is not, and the
 * runtime is captured by reference because there is exactly one per host and the queue is emptied before it is
 * destroyed.
 */
void installAnimationFrameBinding(facebook::jsi::Runtime& runtime, AnimationFrameQueue& animationFrameQueue) {
    runtime.global().setProperty(
        runtime, "requestAnimationFrame",
        facebook::jsi::Function::createFromHostFunction(
            runtime, facebook::jsi::PropNameID::forAscii(runtime, "requestAnimationFrame"), 1,
            [&animationFrameQueue](facebook::jsi::Runtime& callRuntime, const facebook::jsi::Value& /*thisValue*/,
                                   const facebook::jsi::Value* arguments, size_t count) {
                if (count < 1 || !arguments[0].isObject() || !arguments[0].getObject(callRuntime).isFunction(callRuntime)) {
                    throw facebook::jsi::JSError(
                        callRuntime, "The first argument to requestAnimationFrame must be a function.");
                }

                const std::shared_ptr<facebook::jsi::Function> callback =
                    std::make_shared<facebook::jsi::Function>(arguments[0].getObject(callRuntime).getFunction(callRuntime));
                const uint64_t requestHandle =
                    animationFrameQueue.request([&callRuntime, callback](double frameTimestampMilliseconds) {
                        callback->call(callRuntime, frameTimestampMilliseconds);
                    });

                return facebook::jsi::Value(static_cast<double>(requestHandle));
            }));

    runtime.global().setProperty(
        runtime, "cancelAnimationFrame",
        facebook::jsi::Function::createFromHostFunction(
            runtime, facebook::jsi::PropNameID::forAscii(runtime, "cancelAnimationFrame"), 1,
            [&animationFrameQueue](facebook::jsi::Runtime& callRuntime, const facebook::jsi::Value& /*thisValue*/,
                                   const facebook::jsi::Value* arguments, size_t count) {
                if (count < 1 || !arguments[0].isNumber()) {
                    return facebook::jsi::Value::undefined();
                }

                const double requestHandle = arguments[0].getNumber();

                // A negative handle is a no-op per the animation-timing specification, which is also why
                // upstream's `TimerManager::deleteTimer` returns early on one. NaN, either infinity and anything
                // at or above 2^64 are no-ops for a harder reason: converting them to the handle type is
                // undefined behaviour, and JavaScript can pass any of them.
                if (std::isfinite(requestHandle) && requestHandle > 0 && requestHandle < kHandleUpperBound) {
                    animationFrameQueue.cancel(static_cast<uint64_t>(requestHandle));
                }

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

    reactInstance_->initializeRuntime(
        {}, [registry = turboModuleRegistry_.get(), hasMarkedTestPassed = hasMarkedTestPassed_,
             &animationFrameQueue = animationFrameQueue_](facebook::jsi::Runtime& runtime) {
            installConsoleBinding(runtime);
            installMarkTestPassedBinding(runtime, hasMarkedTestPassed);
            installAnimationFrameBinding(runtime, animationFrameQueue);
            registry->install(runtime);
        });
}

ReactHost::~ReactHost() noexcept {
    javaScriptThread_->quitSynchronous();
    animationFrameQueue_.clear();
    turboModuleRegistry_.reset();
    animatedNodesManagerProvider_.reset();

    // `timerManager_` is a second owner of the same upstream `TimerManager` `reactInstance_` was constructed
    // with, and `TimerManager` never clears `timers_` — its map of pending `setTimeout`/`setInterval` callbacks,
    // each a real `jsi::Function` — on `quit()`. `ReactInstance`'s own member order destroys its copy of
    // `timerManager_` before its runtime, which is correct, but that only destroys `TimerManager` itself if
    // it was the last owner. Resetting this copy first makes it so: whichever `.reset()` runs second is the one
    // that actually destroys `TimerManager`, and dropping ours here guarantees that happens no later than
    // `reactInstance_`'s, while the runtime `reactInstance_` owns is still alive. Getting this backwards is
    // what "This PointerValue was left dangling after the Runtime was destroyed" reports.
    timerManager_.reset();
    reactInstance_.reset();
}

facebook::react::ReactInstance& ReactHost::reactInstance() noexcept { return *reactInstance_; }

DimensionsSource& ReactHost::dimensions() noexcept { return turboModuleRegistry_->dimensions(); }

AppearanceModel& ReactHost::appearance() noexcept { return turboModuleRegistry_->appearance(); }

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

void ReactHost::dispatchAnimationFrames(std::chrono::steady_clock::time_point now) {
    if (!animationFrameQueue_.hasPendingRequests()) {
        return;
    }

    const double frameTimestampMilliseconds =
        std::chrono::duration<double, std::milli>(now.time_since_epoch()).count();

    reactInstance_->getBufferedRuntimeExecutor()(
        [&animationFrameQueue = animationFrameQueue_,
         frameTimestampMilliseconds](facebook::jsi::Runtime& /*runtime*/) {
            animationFrameQueue.dispatchFrame(frameTimestampMilliseconds);
        });
}

bool ReactHost::hasPendingTimers() const {
    return timerRegistry_->hasPendingTimers() || animationFrameQueue_.hasPendingRequests();
}

} // namespace react_native_linux
