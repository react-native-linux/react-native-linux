#include "BundleRunner.h"

#include "FabricHost.h"
#include "InputPipeline.h"
#include "ReactHost.h"

#include <cxxreact/JSBigString.h>
#include <react/renderer/graphics/Float.h>
#include <react/renderer/graphics/Point.h>
#include <react/renderer/graphics/Size.h>

#include <chrono>
#include <cstddef>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace react_native_linux {

namespace {

constexpr std::chrono::milliseconds kQuiescenceBudget{30000};
constexpr std::chrono::milliseconds kFirstCommitBudget{10000};
constexpr std::chrono::milliseconds kCommitPollInterval{1};
constexpr char kSmokeSource[] = "console.log('react-native-linux: hermes alive');";
constexpr char kSmokeSourceUrl[] = "smoke.js";
constexpr facebook::react::Size kHeadlessSurfaceSize{.width = 800, .height = 600};
constexpr size_t kInjectedMotionCount = 17;

std::unique_ptr<const facebook::react::JSBigString> readScript(const std::optional<std::string>& bundlePath) {
    if (bundlePath.has_value()) {
        return facebook::react::JSBigFileString::fromPath(bundlePath.value());
    }

    return std::make_unique<facebook::react::JSBigStdString>(kSmokeSource);
}

void loadAndSettle(ReactHost& reactHost, const std::optional<std::string>& bundlePath) {
    std::unique_ptr<const facebook::react::JSBigString> script = readScript(bundlePath);

    reactHost.loadScript(std::move(script), bundlePath.value_or(kSmokeSourceUrl));

    if (!reactHost.runUntilQuiescent(kQuiescenceBudget)) {
        std::cerr << "[bundle-runner] gave up waiting for pending timers" << std::endl;
    }
}

/**
 * Waits for the first commit to reach the scene and returns it, leaving the damage take that came with it behind.
 *
 * There is no callback for "a transaction was mounted", so this polls: drain the JavaScript thread, look, repeat.
 * The bundle's second commit is timer-driven and therefore far away in wall-clock terms, which is what makes a
 * poll good enough to land between the two. A run that misses that window ends with no damage left to take, and
 * the caller reports that rather than producing a golden that proves nothing.
 */
SceneSnapshot waitForFirstCommit(ReactHost& reactHost, FabricHost& fabricHost) {
    const std::chrono::steady_clock::time_point deadline = std::chrono::steady_clock::now() + kFirstCommitBudget;

    while (std::chrono::steady_clock::now() < deadline) {
        reactHost.drainJavaScriptThread();

        SceneFrame frame = fabricHost.takeFrame();

        if (!frame.scene.empty()) {
            return std::move(frame.scene);
        }

        std::this_thread::sleep_for(kCommitPollInterval);
    }

    return {};
}

/**
 * One frame's worth of a mouse sweeping to `surfacePoint`, pushed through the same `InputQueue` the Wayland seat
 * fills. The count is what a 1000 Hz mouse produces inside a 60 Hz frame, rounded up, and the queue is expected to
 * hand back exactly one motion event for it.
 */
std::vector<InputEvent> makeMotionFrame(facebook::react::Point surfacePoint) {
    InputQueue queue;

    for (size_t step = 1; step <= kInjectedMotionCount; ++step) {
        const facebook::react::Float progress =
            static_cast<facebook::react::Float>(step) / static_cast<facebook::react::Float>(kInjectedMotionCount);

        queue.push(InputEvent{
            .kind = InputEventKind::PointerMotion,
            .surfacePoint = facebook::react::Point{.x = surfacePoint.x * progress, .y = surfacePoint.y * progress}});
    }

    return queue.drain();
}

void deliverInputFrame(ReactHost& reactHost, FabricHost& fabricHost, const std::vector<InputEvent>& events) {
    fabricHost.dispatchInput(events);
    fabricHost.induceEventBeat();

    if (!reactHost.runUntilQuiescent(kQuiescenceBudget)) {
        std::cerr << "[bundle-runner] gave up waiting for pending timers" << std::endl;
    }
}

} // namespace

int runInjectedClick(const std::string& bundlePath, facebook::react::Point surfacePoint) {
    ReactHost reactHost;
    std::unique_ptr<FabricHost> fabricHost = std::make_unique<FabricHost>(reactHost.reactInstance(),
                                                                         kHeadlessSurfaceSize);

    reactHost.loadScript(facebook::react::JSBigFileString::fromPath(bundlePath), bundlePath);

    const bool hasCommitted = !waitForFirstCommit(reactHost, *fabricHost).empty();

    if (!hasCommitted) {
        std::cerr << "[bundle-runner] the bundle committed no scene, so there is nothing to click" << std::endl;
    }

    deliverInputFrame(reactHost, *fabricHost, makeMotionFrame(surfacePoint));
    deliverInputFrame(reactHost, *fabricHost,
                      {InputEvent{.kind = InputEventKind::PointerButtonPress, .surfacePoint = surfacePoint}});
    deliverInputFrame(reactHost, *fabricHost,
                      {InputEvent{.kind = InputEventKind::PointerButtonRelease, .surfacePoint = surfacePoint}});

    fabricHost->stopSurface();
    reactHost.drainJavaScriptThread();
    fabricHost.reset();

    return hasCommitted && !reactHost.hasReportedFatalError() ? 0 : 1;
}

FabricDamageRunResult runFabricBundleAcrossCommits(const std::string& bundlePath, facebook::react::Size surfaceSize) {
    ReactHost reactHost;
    std::unique_ptr<FabricHost> fabricHost = std::make_unique<FabricHost>(reactHost.reactInstance(), surfaceSize);

    reactHost.loadScript(facebook::react::JSBigFileString::fromPath(bundlePath), bundlePath);

    FabricDamageRunResult result{.firstScene = waitForFirstCommit(reactHost, *fabricHost)};

    if (!reactHost.runUntilQuiescent(kQuiescenceBudget)) {
        std::cerr << "[bundle-runner] gave up waiting for pending timers" << std::endl;
    }

    SceneFrame secondFrame = fabricHost->takeFrame();

    result.secondScene = std::move(secondFrame.scene);
    result.damage = std::move(secondFrame.damage);

    if (result.firstScene.empty()) {
        result.failure = "the bundle committed no scene before its second commit";
    } else if (result.damage.empty()) {
        result.failure = "the bundle produced no damage after its first commit";
    }

    fabricHost->stopSurface();
    reactHost.drainJavaScriptThread();
    fabricHost.reset();

    result.hasReportedFatalError = reactHost.hasReportedFatalError();

    return result;
}

FabricRunResult runFabricBundle(const std::optional<std::string>& bundlePath, facebook::react::Size surfaceSize) {
    ReactHost reactHost;
    std::unique_ptr<FabricHost> fabricHost = std::make_unique<FabricHost>(reactHost.reactInstance(), surfaceSize);

    loadAndSettle(reactHost, bundlePath);

    SceneSnapshot scene = fabricHost->snapshotScene();
    std::string sceneDump = fabricHost->dumpScene();

    fabricHost->stopSurface();
    reactHost.drainJavaScriptThread();
    fabricHost.reset();

    return FabricRunResult{.scene = std::move(scene),
                           .sceneDump = std::move(sceneDump),
                           .hasReportedFatalError = reactHost.hasReportedFatalError()};
}

int runBundle(const std::optional<std::string>& bundlePath, BundleMode bundleMode) {
    if (bundleMode == BundleMode::Fabric) {
        const FabricRunResult result = runFabricBundle(bundlePath, kHeadlessSurfaceSize);

        std::cout << result.sceneDump << std::flush;

        return result.hasReportedFatalError ? 1 : 0;
    }

    ReactHost reactHost;

    loadAndSettle(reactHost, bundlePath);

    return reactHost.hasReportedFatalError() ? 1 : 0;
}

} // namespace react_native_linux
