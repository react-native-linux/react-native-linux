#include "ReactHost.h"

#include <chrono>
#include <cstddef>
#include <cxxreact/JSBigString.h>
#include <functional>
#include <future>
#include <gtest/gtest.h>
#include <jsi/jsi.h>
#include <memory>
#include <thread>
#include <utility>
#include <vector>

#include <react/bridging/Bridging.h>
#include <react/renderer/runtimescheduler/RuntimeSchedulerCallInvoker.h>

namespace react_native_linux {
namespace {

using facebook::jsi::Runtime;
using facebook::react::AsyncPromise;

constexpr std::size_t kSettlingThreadCount = 8;
constexpr std::size_t kPromisesPerThread = 64;
constexpr std::size_t kPromiseCount = kSettlingThreadCount * kPromisesPerThread;
constexpr std::chrono::milliseconds kQuiescenceBudget{5000};

constexpr char kSettlementLedger[] = R"JAVASCRIPT(
globalThis.resolvedSum = 0;
globalThis.rejectedCount = 0;
globalThis.observe = (promise) => promise.then(
  (value) => { globalThis.resolvedSum += value; },
  () => { globalThis.rejectedCount += 1; });
)JAVASCRIPT";

template <typename Result> Result onJavaScriptThread(ReactHost& reactHost, std::function<Result(Runtime&)> work) {
    std::promise<Result> result;

    reactHost.reactInstance().getBufferedRuntimeExecutor()(
        [&result, &work](Runtime& runtime) { result.set_value(work(runtime)); });

    return result.get_future().get();
}

/**
 * Issue #77, the TurboModule promise leg of `ReactHost`'s threading contract: a module creates an
 * `AsyncPromise` on the JavaScript thread over the host's `RuntimeSchedulerCallInvoker` and hands it to a worker
 * thread, which settles it and then drops it. Settlement and the release of the promise's JSI-owning callbacks
 * therefore both happen off the JavaScript thread, concurrently with the frame thread's own entry points, and the
 * invoker is the only path back to the runtime. Every even promise is resolved with its index and every odd one is
 * rejected; a second settle of the same promise is the late duplicate a module retrying a request produces, and
 * must be a no-op.
 */
TEST(CrossThreadPromiseStressTest, PromisesSettledAndDroppedOffTheJavaScriptThreadReachJavaScriptExactlyOnce) {
    ReactHost reactHost;

    reactHost.loadScript(std::make_unique<facebook::react::JSBigStdString>(kSettlementLedger),
                         "CrossThreadPromiseStressTest.js");

    const std::shared_ptr<facebook::react::CallInvoker> jsInvoker =
        std::make_shared<facebook::react::RuntimeSchedulerCallInvoker>(reactHost.reactInstance().getRuntimeScheduler());

    std::vector<std::vector<AsyncPromise<double>>> promisesPerThread =
        onJavaScriptThread<std::vector<std::vector<AsyncPromise<double>>>>(reactHost, [&jsInvoker](Runtime& runtime) {
            const facebook::jsi::Function observe = runtime.global().getPropertyAsFunction(runtime, "observe");
            std::vector<std::vector<AsyncPromise<double>>> promises(kSettlingThreadCount);

            for (std::vector<AsyncPromise<double>>& threadPromises : promises) {
                for (std::size_t index = 0; index < kPromisesPerThread; ++index) {
                    AsyncPromise<double> promise(runtime, jsInvoker);

                    observe.call(runtime, promise.get(runtime));
                    threadPromises.push_back(std::move(promise));
                }
            }

            return promises;
        });

    std::vector<std::thread> settlingThreads;

    for (std::size_t threadIndex = 0; threadIndex < kSettlingThreadCount; ++threadIndex) {
        settlingThreads.emplace_back([threadIndex, promises = std::move(promisesPerThread[threadIndex])]() mutable {
            for (std::size_t index = 0; index < promises.size(); ++index) {
                const std::size_t promiseNumber = (threadIndex * kPromisesPerThread) + index;

                for (int attempt = 0; attempt < 2; ++attempt) {
                    if (promiseNumber % 2 == 0) {
                        promises[index].resolve(static_cast<double>(promiseNumber));
                    } else {
                        promises[index].reject(facebook::react::Error("rejected off the JavaScript thread"));
                    }
                }
            }
        });
    }

    for (int frame = 0; frame < 100; ++frame) {
        reactHost.dispatchAnimationFrames(std::chrono::steady_clock::now());
        reactHost.publishPendingDimensions();
    }

    for (std::thread& settlingThread : settlingThreads) {
        settlingThread.join();
    }

    ASSERT_TRUE(reactHost.runUntilQuiescent(kQuiescenceBudget));

    const std::pair<double, double> ledger =
        onJavaScriptThread<std::pair<double, double>>(reactHost, [](Runtime& runtime) {
            return std::pair{runtime.global().getProperty(runtime, "resolvedSum").asNumber(),
                             runtime.global().getProperty(runtime, "rejectedCount").asNumber()};
        });

    constexpr double kEvenPromiseCount = kPromiseCount / 2;

    EXPECT_EQ(ledger.first, kEvenPromiseCount * (kEvenPromiseCount - 1));
    EXPECT_EQ(ledger.second, kEvenPromiseCount);
    EXPECT_FALSE(reactHost.hasReportedFatalError());
}

} // namespace
} // namespace react_native_linux
