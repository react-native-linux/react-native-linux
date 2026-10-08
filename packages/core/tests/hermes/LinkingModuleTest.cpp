#include "ReactHost.h"

#include <chrono>
#include <cstdlib>
#include <cxxreact/JSBigString.h>
#include <future>
#include <gtest/gtest.h>
#include <jsi/jsi.h>
#include <memory>
#include <string>

namespace react_native_linux {
namespace {

constexpr std::chrono::milliseconds kQuiescenceBudget{5000};

constexpr char kLinkingProbe[] = R"JAVASCRIPT(
const linking = globalThis.nativeModuleProxy.LinkingManager;
const settle = (promise, key) =>
  promise.then(
    (value) => { globalThis.linkingTrace[key] = `resolved ${value}`; },
    (error) => { globalThis.linkingTrace[key] = `rejected ${error?.message ?? error}`; },
  );

globalThis.linkingTrace = { initialURL: linking.getInitialURL(), events: [] };
globalThis.__rctDeviceEventEmitter = {
  emit: (eventName, payload) => { globalThis.linkingTrace.events.push(`${eventName} ${payload.url}`); },
};
settle(linking.canOpenURL('https://reactnative.dev'), 'canOpenURL');
settle(linking.openURL('https://reactnative.dev'), 'openURL');
settle(linking.openSettings(), 'openSettings');
)JAVASCRIPT";

constexpr char kAfterActivation[] = R"JAVASCRIPT(
globalThis.linkingTrace.urlAfterActivation = globalThis.nativeModuleProxy.LinkingManager.getInitialURL();
)JAVASCRIPT";

std::string traceOf(ReactHost& reactHost) {
    std::promise<std::string> trace;

    reactHost.reactInstance().getBufferedRuntimeExecutor()([&trace](facebook::jsi::Runtime& runtime) {
        const facebook::jsi::Value value = runtime.global().getProperty(runtime, "linkingTrace");

        trace.set_value(runtime.global()
                            .getPropertyAsObject(runtime, "JSON")
                            .getPropertyAsFunction(runtime, "stringify")
                            .call(runtime, value)
                            .getString(runtime)
                            .utf8(runtime));
    });

    return trace.get_future().get();
}

/**
 * #23: `LinkingManager` from JavaScript. No launch URL reads as `null`, a later activation is both the URL
 * `getInitialURL` answers and a `url` device event, `canOpenURL` defers to `xdg-open`, an `xdg-open` that cannot
 * be spawned rejects `openURL` with the spawn error, and `openSettings` rejects because there is nothing to open.
 */
TEST(LinkingModuleTest, AnswersInitialUrlActivationsAndOpenRequests) {
    const std::string path = std::getenv("PATH") == nullptr ? "" : std::getenv("PATH");
    ReactHost reactHost;

    ASSERT_EQ(setenv("PATH", "", 1), 0);
    reactHost.loadScript(std::make_unique<facebook::react::JSBigStdString>(kLinkingProbe), "LinkingModuleTest.js");
    reactHost.drainJavaScriptThread();
    ASSERT_EQ(setenv("PATH", path.c_str(), 1), 0);

    reactHost.activation().onActivationUrlReceived("myapp://deep/link");
    reactHost.loadScript(std::make_unique<facebook::react::JSBigStdString>(kAfterActivation), "LinkingAfter.js");
    EXPECT_TRUE(reactHost.runUntilQuiescent(kQuiescenceBudget));

    EXPECT_EQ(traceOf(reactHost),
              R"({"initialURL":null,"events":["url myapp://deep/link"],"canOpenURL":"resolved true",)"
              R"("openURL":"rejected No such file or directory",)"
              R"("openSettings":"rejected openSettings has no desktop equivalent on Linux",)"
              R"("urlAfterActivation":"myapp://deep/link"})");
    EXPECT_FALSE(reactHost.hasReportedFatalError());
}

} // namespace
} // namespace react_native_linux
