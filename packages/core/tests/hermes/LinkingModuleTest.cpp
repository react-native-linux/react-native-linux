#include "ReactHost.h"

#include <chrono>
#include <csignal>
#include <cstdlib>
#include <cxxreact/JSBigString.h>
#include <filesystem>
#include <fstream>
#include <future>
#include <gtest/gtest.h>
#include <jsi/jsi.h>
#include <memory>
#include <string>
#include <thread>
#include <unistd.h>

namespace react_native_linux {
namespace {

constexpr std::chrono::milliseconds kQuiescenceBudget{5000};
constexpr std::chrono::milliseconds kPollInterval{20};

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

constexpr char kOpenerOutcomesProbe[] = R"JAVASCRIPT(
const linking = globalThis.nativeModuleProxy.LinkingManager;
globalThis.linkingTrace = {};
['exit:0', 'exit:3', 'exit:9', 'kill', 'block'].forEach((url) =>
  linking.openURL(url).then(
    () => { globalThis.linkingTrace[url] = 'resolved'; },
    (error) => { globalThis.linkingTrace[url] = `rejected ${error.message}`; },
  ),
);
)JAVASCRIPT";

constexpr char kFakeOpener[] = R"SHELL(#!/bin/sh
case "$1" in
  exit:*) exit "${1#exit:}" ;;
  kill) kill -KILL $$ ;;
  block) echo $$ > "$(dirname "$0")/blocked.pid"; exec sleep 60 ;;
esac
)SHELL";

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

/**
 * #768: `openURL` settles on how `xdg-open` ends, through a fake one on `PATH`. Exit 0 resolves; exit 3 rejects with
 * the reason `xdg-open(1)` gives that status; an undocumented status and a killed opener reject with the raw status;
 * an opener still running after the wait resolves, and is reaped once it does end rather than left a zombie.
 */
TEST(LinkingModuleTest, SettlesOpenUrlOnHowXdgOpenEnds) {
    const std::filesystem::path openerDirectory =
        std::filesystem::temp_directory_path() / ("rnl-linking-opener-" + std::to_string(::getpid()));
    const std::filesystem::path opener = openerDirectory / "xdg-open";
    const std::string path = std::getenv("PATH") == nullptr ? "" : std::getenv("PATH");
    ReactHost reactHost;

    std::filesystem::create_directories(openerDirectory);
    std::ofstream openerScript(opener);
    openerScript << kFakeOpener;
    openerScript.close();
    std::filesystem::permissions(opener, std::filesystem::perms::owner_all);
    ASSERT_EQ(setenv("PATH", (openerDirectory.string() + ":" + path).c_str(), 1), 0);
    reactHost.loadScript(std::make_unique<facebook::react::JSBigStdString>(kOpenerOutcomesProbe),
                         "LinkingOpenerOutcomes.js");
    reactHost.drainJavaScriptThread();
    ASSERT_EQ(setenv("PATH", path.c_str(), 1), 0);

    const std::string expected =
        R"json({"exit:0":"resolved",)json"
        R"json("exit:3":"rejected Unable to open URL: exit:3 (xdg-open: a required tool could not be found)",)json"
        R"json("exit:9":"rejected Unable to open URL: exit:9 (xdg-open ended with status 9)",)json"
        R"json("kill":"rejected Unable to open URL: kill (xdg-open ended with status 9)","block":"resolved"})json";
    const std::chrono::steady_clock::time_point deadline = std::chrono::steady_clock::now() + kQuiescenceBudget;
    std::string trace = traceOf(reactHost);

    while (trace != expected && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(kPollInterval);
        trace = traceOf(reactHost);
    }

    EXPECT_EQ(trace, expected);

    pid_t blockedOpener = 0;
    std::ifstream blockedPidFile(openerDirectory / "blocked.pid");
    blockedPidFile >> blockedOpener;
    const std::filesystem::path blockedProcess = "/proc/" + std::to_string(blockedOpener);

    ASSERT_GT(blockedOpener, 0);
    ASSERT_TRUE(std::filesystem::exists(blockedProcess));
    ASSERT_EQ(kill(blockedOpener, SIGKILL), 0);

    while (std::filesystem::exists(blockedProcess) && std::chrono::steady_clock::now() < deadline + kQuiescenceBudget) {
        std::this_thread::sleep_for(kPollInterval);
    }

    EXPECT_FALSE(std::filesystem::exists(blockedProcess));
    EXPECT_FALSE(reactHost.hasReportedFatalError());
    std::filesystem::remove_all(openerDirectory);
}

} // namespace
} // namespace react_native_linux
