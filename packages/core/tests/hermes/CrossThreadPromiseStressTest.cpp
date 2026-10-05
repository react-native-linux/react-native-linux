#include "FabricHost.h"
#include "ImageDecoder.h"
#include "InputPipeline.h"
#include "ReactHost.h"

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cxxreact/JSBigString.h>
#include <filesystem>
#include <fstream>
#include <functional>
#include <future>
#include <gtest/gtest.h>
#include <jsi/jsi.h>
#include <latch>
#include <memory>
#include <string>
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
constexpr int kFrameCount = 100;
constexpr std::chrono::milliseconds kQuiescenceBudget{5000};

constexpr char kSettlementLedger[] = R"JAVASCRIPT(
globalThis.resolvedSum = 0;
globalThis.rejectedCount = 0;
globalThis.frameCount = 0;
const onFrame = () => {
  globalThis.frameCount += 1;
  requestAnimationFrame(onFrame);
};
requestAnimationFrame(onFrame);
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
 * therefore both happen off the JavaScript thread, concurrently with the frame thread dispatching a live
 * `requestAnimationFrame` chain and publishing a dimensions change every frame, and the
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

    std::latch start{static_cast<std::ptrdiff_t>(kSettlingThreadCount) + 1};
    std::vector<std::thread> settlingThreads;

    for (std::size_t threadIndex = 0; threadIndex < kSettlingThreadCount; ++threadIndex) {
        settlingThreads.emplace_back(
            [&start, threadIndex, promises = std::move(promisesPerThread[threadIndex])]() mutable {
                start.arrive_and_wait();

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

    start.arrive_and_wait();

    for (int frame = 0; frame < kFrameCount; ++frame) {
        reactHost.dimensions().configure(800.0 + frame, 600.0, 1.0);
        reactHost.publishPendingDimensions();
        reactHost.dispatchAnimationFrames(std::chrono::steady_clock::now());
    }

    for (std::thread& settlingThread : settlingThreads) {
        settlingThread.join();
    }

    ASSERT_TRUE(reactHost.runUntilQuiescent(kQuiescenceBudget));

    const std::array<double, 3> ledger = onJavaScriptThread<std::array<double, 3>>(reactHost, [](Runtime& runtime) {
        return std::array{runtime.global().getProperty(runtime, "resolvedSum").asNumber(),
                          runtime.global().getProperty(runtime, "rejectedCount").asNumber(),
                          runtime.global().getProperty(runtime, "frameCount").asNumber()};
    });

    constexpr double kEvenPromiseCount = kPromiseCount / 2;

    EXPECT_EQ(ledger[0], kEvenPromiseCount * (kEvenPromiseCount - 1));
    EXPECT_EQ(ledger[1], kEvenPromiseCount);
    EXPECT_GT(ledger[2], 0.0);
    EXPECT_FALSE(reactHost.hasReportedFatalError());
}

constexpr std::size_t kImageFileCount = 64;
constexpr int kMountingFrameCount = 200;
constexpr double kFrameMilliseconds = 16.0;

// Unpaced, the loop finishes before the JavaScript thread has evaluated the script, and every frame then races an
// empty surface; a millisecond lets commits land between frames, so input hits mounted images and decodes complete
// into nodes the frame thread is reading.
constexpr std::chrono::milliseconds kFramePause{1};

// A 1x1 RGBA PNG, written to a file per URI because the decode cache is keyed by URI: one file is one decode.
constexpr std::array<std::uint8_t, 70> kOnePixelPng{
    0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A, 0x00, 0x00, 0x00, 0x0D, 0x49, 0x48, 0x44, 0x52, 0x00, 0x00,
    0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x08, 0x06, 0x00, 0x00, 0x00, 0x1F, 0x15, 0xC4, 0x89, 0x00, 0x00, 0x00,
    0x0D, 0x49, 0x44, 0x41, 0x54, 0x78, 0x9C, 0x63, 0x50, 0x70, 0x68, 0xF8, 0x0F, 0x00, 0x03, 0x44, 0x01, 0xE0,
    0x32, 0xAA, 0xBF, 0x84, 0x00, 0x00, 0x00, 0x00, 0x49, 0x45, 0x4E, 0x44, 0xAE, 0x42, 0x60, 0x82};

// Every animation frame commits a new tree of eight images whose sources walk through the files, so mounts
// request decodes on the JavaScript thread for as long as there are files left and keep replacing the nodes the
// decodes complete into after that.
constexpr char kMountingLoop[] = R"JAVASCRIPT(
const fabric = globalThis.nativeFabricUIManager;
globalThis.deliveredEvents = 0;
globalThis.commitCount = 0;
fabric.registerEventHandler(() => { globalThis.deliveredEvents += 1; });
let nextTag = 2;
const createNode = (componentName, props) => {
  const instanceHandle = {};
  globalThis.instanceHandles.push(instanceHandle);
  const tag = nextTag;
  nextTag += 2;
  return fabric.createNode(tag, componentName, 1, props, instanceHandle);
};
const box = (left, top) => ({ height: 100, left, position: 'absolute', top, width: 100 });
const commit = () => {
  globalThis.instanceHandles = [];
  const childSet = fabric.createChildSet();
  for (let index = 0; index < 8; index += 1) {
    const file = (globalThis.commitCount * 8 + index) % globalThis.imageFileCount;
    fabric.appendChildToSet(childSet, createNode('Image', {
      source: [{ uri: `file://${globalThis.imageDirectory}/${file}.png` }],
      ...box((index % 4) * 100, Math.floor(index / 4) * 100),
    }));
  }
  fabric.completeRoot(1, childSet);
  globalThis.commitCount += 1;
  requestAnimationFrame(commit);
};
requestAnimationFrame(commit);
)JAVASCRIPT";

/**
 * Issue #77, the mounting, image-decode and input legs of `FabricHost`'s threading contract: the JavaScript thread
 * commits a new tree of images every animation frame, each mount requests decodes that complete on the decode
 * worker and damage the scene through the mounting manager's listener, and this thread is the frame thread,
 * calling every member the contract lets the frame loop call while the surface runs — in `WindowSession`'s order —
 * with a press, a release and a motion injected every frame. TSan grades the races; the assertions grade that
 * every leg did its work rather than none of it.
 */
TEST(CrossThreadMountingStressTest, MountsDecodesAndInputRunConcurrentlyWithTheFrameThread) {
    const std::filesystem::path imageDirectory =
        std::filesystem::temp_directory_path() / "rnl-cross-thread-mounting-stress";

    std::filesystem::remove_all(imageDirectory);
    std::filesystem::create_directories(imageDirectory);

    for (std::size_t file = 0; file < kImageFileCount; ++file) {
        std::ofstream(imageDirectory / (std::to_string(file) + ".png"), std::ios::binary)
            .write(reinterpret_cast<const char*>(kOnePixelPng.data()), kOnePixelPng.size());
    }

    ReactHost reactHost;
    auto fabricHost =
        std::make_unique<FabricHost>(reactHost.reactInstance(), facebook::react::Size{.width = 400, .height = 200});

    reactHost.loadScript(std::make_unique<facebook::react::JSBigStdString>(
                             "globalThis.imageDirectory = '" + imageDirectory.string() +
                             "';\nglobalThis.imageFileCount = " + std::to_string(kImageFileCount) + ";\n" +
                             kMountingLoop),
                         "CrossThreadMountingStressTest.js");

    for (int frame = 0; frame < kMountingFrameCount; ++frame) {
        const auto now = std::chrono::steady_clock::now();
        const facebook::react::Point point{.x = 50.0F + static_cast<float>(frame % 4) * 100.0F, .y = 50.0F};

        fabricHost->advanceImageAnimations(kFrameMilliseconds);
        fabricHost->dispatchInput({InputEvent{.kind = InputEventKind::PointerMotion, .surfacePoint = point},
                                   InputEvent{.kind = InputEventKind::PointerButtonPress, .surfacePoint = point},
                                   InputEvent{.kind = InputEventKind::PointerButtonRelease, .surfacePoint = point}});
        fabricHost->advanceScroll(kFrameMilliseconds);
        fabricHost->induceEventBeat();
        fabricHost->tickAnimations(now);
        reactHost.dispatchAnimationFrames(now);
        static_cast<void>(fabricHost->takeFrame());
        static_cast<void>(fabricHost->findNodeAtPoint(point));
        std::this_thread::sleep_for(kFramePause);
    }

    ASSERT_TRUE(waitForPendingImageDecodes(kQuiescenceBudget));
    ASSERT_TRUE(reactHost.runUntilQuiescent(kQuiescenceBudget));

    const std::array<double, 2> ledger = onJavaScriptThread<std::array<double, 2>>(reactHost, [](Runtime& runtime) {
        return std::array{runtime.global().getProperty(runtime, "commitCount").asNumber(),
                          runtime.global().getProperty(runtime, "deliveredEvents").asNumber()};
    });

    fabricHost->stopSurface();
    reactHost.drainJavaScriptThread();
    fabricHost.reset();

    for (std::size_t file = 0; file < kImageFileCount; ++file) {
        EXPECT_NE(decodedImage("file://" + (imageDirectory / (std::to_string(file) + ".png")).string()), nullptr)
            << file;
    }

    EXPECT_GT(ledger[0], static_cast<double>(kImageFileCount) / 8);
    EXPECT_GT(ledger[1], 0.0);
    EXPECT_FALSE(reactHost.hasReportedFatalError());
    std::filesystem::remove_all(imageDirectory);
}

} // namespace
} // namespace react_native_linux
