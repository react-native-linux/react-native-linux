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
#include <utility>

namespace react_native_linux {

namespace {

constexpr std::chrono::milliseconds kQuiescenceBudget{30000};
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

} // namespace

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
