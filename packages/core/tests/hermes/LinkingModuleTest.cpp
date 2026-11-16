#include "../ScopedEnvironment.h"
#include "ReactHost.h"

#include <chrono>
#include <cstdlib>
#include <cxxreact/JSBigString.h>
#include <filesystem>
#include <fstream>
#include <future>
#include <gtest/gtest.h>
#include <jsi/jsi.h>
#include <memory>
#include <signal.h>
#include <string>
#include <sys/syscall.h>
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
['exit:0', 'exit:3', 'exit:9', 'kill', 'late:3', 'block'].forEach((url) =>
  linking.openURL(url).then(
    () => { globalThis.linkingTrace[url] = 'resolved'; },
    (error) => { globalThis.linkingTrace[url] = `rejected ${error.message}`; },
  ),
);
)JAVASCRIPT";

constexpr char kFakeOpener[] = R"SHELL(#!/bin/sh
case "$1" in
  exit:*) exit "${1#exit:}" ;;
  late:*) sleep 1; exit "${1#late:}" ;;
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
    ReactHost reactHost;

    {
        const ScopedEnvironment emptyPath("PATH", "");

        reactHost.loadScript(std::make_unique<facebook::react::JSBigStdString>(kLinkingProbe), "LinkingModuleTest.js");
        reactHost.drainJavaScriptThread();
    }

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
 * A fake `xdg-open` first on `PATH`, and a no-op `SIGUSR1` handler without `SA_RESTART`, both undone however the test
 * ends, along with the opener a `block` call leaves running.
 */
class LinkingOpenerTest : public ::testing::Test {
protected:
    void SetUp() override {
        const char* path = std::getenv("PATH");

        savedPath_ = path == nullptr ? "" : path;
        std::filesystem::create_directories(openerDirectory_);
        std::ofstream openerScript(openerDirectory_ / "xdg-open");
        openerScript << kFakeOpener;
        openerScript.close();
        std::filesystem::permissions(openerDirectory_ / "xdg-open", std::filesystem::perms::owner_all);
        setenv("PATH", (openerDirectory_.string() + ":" + savedPath_).c_str(), 1);

        struct sigaction interrupting {};

        interrupting.sa_handler = [](int) {};
        sigaction(SIGUSR1, &interrupting, &savedAction_);
    }

    void TearDown() override {
        killBlockedOpener();
        sigaction(SIGUSR1, &savedAction_, nullptr);
        setenv("PATH", savedPath_.c_str(), 1);
        std::filesystem::remove_all(openerDirectory_);
    }

    /** Kills the opener a `block` call left running, once, if there is one; its process id, or 0. */
    pid_t killBlockedOpener() const {
        pid_t blockedOpener = 0;
        std::ifstream blockedPidFile(openerDirectory_ / "blocked.pid");

        blockedPidFile >> blockedOpener;
        std::filesystem::remove(openerDirectory_ / "blocked.pid");

        return blockedOpener > 0 && kill(blockedOpener, SIGKILL) == 0 ? blockedOpener : 0;
    }

    const std::filesystem::path openerDirectory_ =
        std::filesystem::temp_directory_path() / ("rnl-linking-opener-" + std::to_string(::getpid()));
    std::string savedPath_;

    struct sigaction savedAction_ {};
};

/** Sends `SIGUSR1` to every thread of this process blocked in `syscallNumber`; whether it reached any. */
bool interruptThreadsIn(long syscallNumber) {
    bool interrupted = false;

    for (const std::filesystem::directory_entry& task : std::filesystem::directory_iterator("/proc/self/task")) {
        long blockedIn = -1;
        std::ifstream(task.path() / "syscall") >> blockedIn;

        if (blockedIn == syscallNumber &&
            tgkill(::getpid(), std::stoi(task.path().filename().string()), SIGUSR1) == 0) {
            interrupted = true;
        }
    }

    return interrupted;
}

/**
 * #768: `openURL` settles on how `xdg-open` ends. Exit 0 resolves; exit 3 rejects with the reason `xdg-open(1)` gives
 * that status; an undocumented status rejects with it, and a killed opener with its signal; an opener still running
 * after the wait resolves, and is reaped once it does end rather than left a zombie. A signal that interrupts the
 * worker's `poll` while `late:3` runs, or the reaper's `waitid` while `block` runs, changes none of that.
 */
TEST_F(LinkingOpenerTest, SettlesOpenUrlOnHowXdgOpenEndsThroughInterruptingSignals) {
    ReactHost reactHost;

    reactHost.loadScript(std::make_unique<facebook::react::JSBigStdString>(kOpenerOutcomesProbe),
                         "LinkingOpenerOutcomes.js");
    reactHost.drainJavaScriptThread();

    const std::string expected =
        R"json({"exit:0":"resolved",)json"
        R"json("exit:3":"rejected Unable to open URL: exit:3 (xdg-open: a required tool could not be found)",)json"
        R"json("exit:9":"rejected Unable to open URL: exit:9 (xdg-open exited with status 9)",)json"
        R"json("kill":"rejected Unable to open URL: kill (xdg-open was killed by signal 9)",)json"
        R"json("late:3":"rejected Unable to open URL: late:3 (xdg-open: a required tool could not be found)",)json"
        R"json("block":"resolved"})json";
    const std::chrono::steady_clock::time_point deadline = std::chrono::steady_clock::now() + kQuiescenceBudget;
    bool interruptedPoll = false;
    bool interruptedWait = false;
    std::string trace = traceOf(reactHost);

    while ((trace != expected || !interruptedWait) && std::chrono::steady_clock::now() < deadline) {
        interruptedPoll = interruptThreadsIn(SYS_poll) || interruptedPoll;
        interruptedWait = interruptThreadsIn(SYS_waitid) || interruptedWait;
        std::this_thread::sleep_for(kPollInterval);
        trace = traceOf(reactHost);
    }

    const pid_t blockedOpener = killBlockedOpener();
    const std::filesystem::path blockedProcess = "/proc/" + std::to_string(blockedOpener);

    while (blockedOpener > 0 && std::filesystem::exists(blockedProcess) &&
           std::chrono::steady_clock::now() < deadline + kQuiescenceBudget) {
        std::this_thread::sleep_for(kPollInterval);
    }

    EXPECT_EQ(trace, expected);
    EXPECT_TRUE(interruptedPoll);
    EXPECT_TRUE(interruptedWait);
    EXPECT_GT(blockedOpener, 0);
    EXPECT_FALSE(std::filesystem::exists(blockedProcess));
    EXPECT_FALSE(reactHost.hasReportedFatalError());
}

} // namespace
} // namespace react_native_linux
