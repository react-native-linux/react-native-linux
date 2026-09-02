#include "BundleRunner.h"

#include "FabricHost.h"
#include "ReactHost.h"

#include <cxxreact/JSBigString.h>
#include <react/renderer/graphics/Size.h>

#include <chrono>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <utility>

namespace react_native_linux {

namespace {

constexpr std::chrono::milliseconds kQuiescenceBudget{30000};
constexpr std::chrono::milliseconds kFirstCommitBudget{10000};
constexpr std::chrono::milliseconds kCommitPollInterval{1};
constexpr char kSmokeSource[] = "console.log('react-native-linux: hermes alive');";
constexpr char kSmokeSourceUrl[] = "smoke.js";
constexpr facebook::react::Size kHeadlessSurfaceSize{.width = 800, .height = 600};

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

} // namespace

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
