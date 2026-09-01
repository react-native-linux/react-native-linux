#include "BundleRunner.h"

#include "ConsoleBinding.h"
#include "FabricHost.h"
#include "HermesJSRuntimeFactory.h"
#include "HostTimerRegistry.h"
#include "JsErrorReporter.h"

#include <cxxreact/JSBigString.h>
#include <react/featureflags/ReactNativeFeatureFlags.h>
#include <react/featureflags/ReactNativeFeatureFlagsOverridesOSSStable.h>
#include <react/renderer/graphics/Size.h>
#include <react/runtime/ReactInstance.h>
#include <react/runtime/TimerManager.h>
#include <react/threading/MessageQueueThreadImpl.h>

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

std::chrono::milliseconds remainingBudget(std::chrono::steady_clock::time_point deadline) {
    return std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now());
}

} // namespace

int runBundle(const std::optional<std::string>& bundlePath, BundleMode bundleMode) {
    std::unique_ptr<const facebook::react::JSBigString> script = readScript(bundlePath);
    const std::string sourceUrl = bundlePath.value_or(kSmokeSourceUrl);

    facebook::react::ReactNativeFeatureFlags::override(
        std::make_unique<facebook::react::ReactNativeFeatureFlagsOverridesOSSStable>());

    const std::shared_ptr<facebook::react::MessageQueueThreadImpl> jsMessageQueueThread =
        std::make_shared<facebook::react::MessageQueueThreadImpl>();

    std::unique_ptr<HostTimerRegistry> ownedTimerRegistry = std::make_unique<HostTimerRegistry>();
    HostTimerRegistry& timerRegistry = *ownedTimerRegistry;
    const std::shared_ptr<facebook::react::TimerManager> timerManager =
        std::make_shared<facebook::react::TimerManager>(std::move(ownedTimerRegistry));
    timerRegistry.setTimerManager(timerManager);

    JsErrorReporter errorReporter;
    HermesJSRuntimeFactory runtimeFactory;

    std::unique_ptr<facebook::react::ReactInstance> reactInstance = std::make_unique<facebook::react::ReactInstance>(
        runtimeFactory.createJSRuntime(jsMessageQueueThread), jsMessageQueueThread, timerManager,
        errorReporter.createHandler());
    timerManager->setRuntimeExecutor(reactInstance->getBufferedRuntimeExecutor());

    reactInstance->initializeRuntime({}, installConsoleBinding);

    std::unique_ptr<FabricHost> fabricHost;

    if (bundleMode == BundleMode::Fabric) {
        fabricHost = std::make_unique<FabricHost>(*reactInstance, kHeadlessSurfaceSize);
    }

    reactInstance->loadScript(std::move(script), sourceUrl);

    const std::chrono::steady_clock::time_point deadline = std::chrono::steady_clock::now() + kQuiescenceBudget;
    bool isQuiescent = false;

    while (!isQuiescent) {
        jsMessageQueueThread->runOnQueueSync([]() {});

        if (!timerRegistry.hasPendingTimers()) {
            isQuiescent = true;
        } else if (!timerRegistry.waitUntilIdle(remainingBudget(deadline))) {
            std::cerr << "[bundle-runner] gave up waiting for pending timers" << std::endl;
            isQuiescent = true;
        }
    }

    if (fabricHost) {
        std::cout << fabricHost->dumpScene() << std::flush;
        fabricHost->stopSurface();
        jsMessageQueueThread->runOnQueueSync([]() {});
        fabricHost.reset();
    }

    jsMessageQueueThread->quitSynchronous();
    reactInstance.reset();

    return errorReporter.hasReportedFatalError() ? 1 : 0;
}

} // namespace react_native_linux
