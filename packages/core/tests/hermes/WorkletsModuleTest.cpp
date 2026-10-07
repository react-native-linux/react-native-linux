#include "WorkletsModule.h"

#include "ReactHost.h"

#include <chrono>
#include <cxxreact/JSBigString.h>
#include <future>
#include <gtest/gtest.h>
#include <memory>
#include <string>
#include <worklets/WorkletRuntime/WorkletRuntime.h>

namespace react_native_linux {
namespace {

// The six unpackers worklets' JavaScript loads before `start` are stubs here, one of them seeding the registry the
// UI runtime reads custom serializables from: this suite proves the host, and the
// worklets JavaScript that serializes real worklets arrives with the Babel plugin and Metro resolution of #137.
constexpr char kInstallAndStart[] = R"JAVASCRIPT(
const worklets = globalThis.nativeModuleProxy.WorkletsModule;
const attempt = (call) => { try { return `returned ${call()}`; } catch (error) { return `threw ${error.message}`; } };
const stub = ['(function () {})', 'stub.js', ''];
const registry = ['(function () { globalThis.__customSerializationRegistry = []; })', 'registry.js', ''];

globalThis.workletsTrace = {
  bundleMode: attempt(() => worklets.installTurboModule(true)),
  install: attempt(() => worklets.installTurboModule(false)),
  proxy: typeof globalThis.__workletsModuleProxy,
};
globalThis.__workletsModuleProxy.loadUnpackersWithCode(...registry, ...stub, ...stub, ...stub, ...stub, ...stub);
globalThis.workletsTrace.start = attempt(() => worklets.start());
globalThis.workletsTrace.slowAnimations = attempt(() => worklets.toggleSlowAnimationsOnUIRuntime());
)JAVASCRIPT";

constexpr char kRequestTwoFrames[] = R"JAVASCRIPT(
globalThis.frames = [];
globalThis.__nativeRequestAnimationFrame((first) => {
  globalThis.frames.push(first);
  globalThis.__nativeRequestAnimationFrame((second) => { globalThis.frames.push(second); });
});
globalThis.frames;
)JAVASCRIPT";

constexpr std::chrono::steady_clock::time_point kFirstTick{std::chrono::milliseconds(1000)};
constexpr std::chrono::steady_clock::time_point kSecondTick{std::chrono::milliseconds(1016)};

std::string javaScriptTrace(ReactHost& reactHost) {
    std::promise<std::string> trace;

    reactHost.reactInstance().getBufferedRuntimeExecutor()([&trace](facebook::jsi::Runtime& runtime) {
        trace.set_value(runtime.global()
                            .getPropertyAsObject(runtime, "JSON")
                            .getPropertyAsFunction(runtime, "stringify")
                            .call(runtime, runtime.global().getProperty(runtime, "workletsTrace"))
                            .getString(runtime)
                            .utf8(runtime));
    });

    return trace.get_future().get();
}

void startWorklets(ReactHost& reactHost) {
    reactHost.loadScript(std::make_unique<facebook::react::JSBigStdString>(kInstallAndStart), "WorkletsModuleTest.js");
    reactHost.drainJavaScriptThread();
}

std::string evaluateOnUIRuntime(worklets::WorkletRuntime& uiRuntime, const char* source) {
    return uiRuntime.runSync([source](facebook::jsi::Runtime& runtime) {
        return runtime.global()
            .getPropertyAsObject(runtime, "JSON")
            .getPropertyAsFunction(runtime, "stringify")
            .call(runtime, runtime.evaluateJavaScript(std::make_shared<facebook::jsi::StringBuffer>(source), "ui.js"))
            .getString(runtime)
            .utf8(runtime);
    });
}

/**
 * #136: JavaScript installs the host as worklets' own `NativeWorklets` does. Bundle Mode is refused, the proxy
 * global appears, `start` initializes the UI runtime, and the iOS-only toggle throws as it does on iOS.
 */
TEST(WorkletsModuleTest, JavaScriptInstallsAndStartsTheProxy) {
    ReactHost reactHost;

    EXPECT_EQ(reactHost.worklets().uiWorkletRuntime(), nullptr);

    startWorklets(reactHost);

    EXPECT_EQ(
        javaScriptTrace(reactHost),
        R"({"bundleMode":"threw Exception in HostFunction: [Worklets] Bundle Mode is not supported on Linux yet.",)"
        R"("install":"returned true","proxy":"object","start":"returned true",)"
        R"("slowAnimations":"threw Exception in HostFunction: [Worklets] toggleSlowAnimationsOnUIRuntime is not )"
        R"(supported on Linux."})");
    EXPECT_NE(reactHost.worklets().uiWorkletRuntime(), nullptr);
    EXPECT_FALSE(reactHost.hasReportedFatalError());
}

/**
 * #136: a `requestAnimationFrame` from the UI worklet runtime fires on the next frame with that frame's timestamp,
 * and one registered while it fires waits for the frame after, rather than running re-entrantly.
 */
TEST(WorkletsModuleTest, UIRuntimeAnimationFramesFireOnTheNextTickWithItsTimestamp) {
    ReactHost reactHost;

    startWorklets(reactHost);
    const auto uiRuntime = reactHost.worklets().uiWorkletRuntime();
    ASSERT_NE(uiRuntime, nullptr);

    EXPECT_FALSE(reactHost.worklets().hasPendingWork());
    evaluateOnUIRuntime(*uiRuntime, kRequestTwoFrames);
    EXPECT_TRUE(reactHost.hasPendingTimers());

    reactHost.dispatchAnimationFrames(kFirstTick);
    EXPECT_EQ(evaluateOnUIRuntime(*uiRuntime, "globalThis.frames"), "[1000]");
    EXPECT_TRUE(reactHost.worklets().hasPendingWork());

    reactHost.dispatchAnimationFrames(kSecondTick);
    EXPECT_EQ(evaluateOnUIRuntime(*uiRuntime, "globalThis.frames"), "[1000,1016]");
    EXPECT_FALSE(reactHost.worklets().hasPendingWork());
}

/** #136, run under ASan and TSan: an instance torn down with a worklet frame still pending leaves nothing behind. */
TEST(WorkletsModuleTest, TeardownWithAFramePendingIsClean) {
    auto reactHost = std::make_unique<ReactHost>();

    startWorklets(*reactHost);
    evaluateOnUIRuntime(*reactHost->worklets().uiWorkletRuntime(), kRequestTwoFrames);
    ASSERT_TRUE(reactHost->worklets().hasPendingWork());

    reactHost.reset();
}

} // namespace
} // namespace react_native_linux
