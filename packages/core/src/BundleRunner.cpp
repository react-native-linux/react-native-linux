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

} // namespace

int runBundle(const std::optional<std::string>& bundlePath, BundleMode bundleMode) {
    std::unique_ptr<const facebook::react::JSBigString> script = readScript(bundlePath);
    const std::string sourceUrl = bundlePath.value_or(kSmokeSourceUrl);

    ReactHost reactHost;
    std::unique_ptr<FabricHost> fabricHost;

    if (bundleMode == BundleMode::Fabric) {
        fabricHost = std::make_unique<FabricHost>(reactHost.reactInstance(), kHeadlessSurfaceSize);
    }

    reactHost.loadScript(std::move(script), sourceUrl);

    if (!reactHost.runUntilQuiescent(kQuiescenceBudget)) {
        std::cerr << "[bundle-runner] gave up waiting for pending timers" << std::endl;
    }

    if (fabricHost) {
        std::cout << fabricHost->dumpScene() << std::flush;
        fabricHost->stopSurface();
        reactHost.drainJavaScriptThread();
        fabricHost.reset();
    }

    return reactHost.hasReportedFatalError() ? 1 : 0;
}

} // namespace react_native_linux
