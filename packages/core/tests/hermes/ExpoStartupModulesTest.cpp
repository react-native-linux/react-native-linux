#include "Activation.h"
#include "OnJavaScriptThread.h"
#include "ReactHost.h"

#include <array>
#include <chrono>
#include <climits>
#include <cstdio>
#include <cxxreact/JSBigString.h>
#include <gtest/gtest.h>
#include <memory>
#include <string>
#include <sys/utsname.h>
#include <unistd.h>

namespace react_native_linux {
namespace {

constexpr std::chrono::milliseconds kQuiescenceBudget{5000};

/**
 * Loads `probe`, which sets `globalThis.trace` to a JSON string, runs it until it settles, and answers the trace, or
 * `threw: <message>` when the probe throws, as it does when the module is not registered.
 */
std::string settledTrace(ReactHost& reactHost, const std::string& probe) {
    const std::string guardedProbe =
        "(async () => {\n" + probe + "\n})().catch((error) => { globalThis.trace = `threw: ${error.message}`; });";

    reactHost.loadScript(std::make_unique<facebook::react::JSBigStdString>(guardedProbe), "ExpoStartupModulesTest.js");
    EXPECT_TRUE(reactHost.runUntilQuiescent(kQuiescenceBudget));
    EXPECT_FALSE(reactHost.hasReportedFatalError());

    return onJavaScriptThread<std::string>(reactHost, [](facebook::jsi::Runtime& runtime) {
        const facebook::jsi::Value trace = runtime.global().getProperty(runtime, "trace");

        return trace.isString() ? trace.getString(runtime).utf8(runtime) : std::string("the probe never finished");
    });
}

std::string settledTrace(const std::string& probe) {
    ReactHost reactHost;

    return settledTrace(reactHost, probe);
}

/** What a shell prints for `command`, without its trailing newline, as a JSON string, or `null` when it is empty. */
std::string shellOutputAsJson(const std::string& command) {
    std::array<char, PATH_MAX> buffer{};
    std::string output;
    FILE* pipe = popen(command.c_str(), "r");

    while (fgets(buffer.data(), buffer.size(), pipe) != nullptr) {
        output += buffer.data();
    }

    pclose(pipe);

    if (!output.empty() && output.back() == '\n') {
        output.pop_back();
    }

    return output.empty() ? "null" : "\"" + output + "\"";
}

/**
 * ADR-0004: upstream's `expo-linking` reads `ExpoLinking` as it is imported. `getLinkingURL` follows the activation
 * state React Native's `Linking` reads, and an `onURLReceived` listener receives upstream's `{ url }` payload through
 * the `url` device event a later activation emits.
 */
TEST(ExpoStartupModulesTest, LinkingAnswersTheActivationUrlAndItsDeviceEvent) {
    ReactHost reactHost;
    const std::string beforeActivation = settledTrace(reactHost, R"JAVASCRIPT(
const linking = globalThis.nativeModuleProxy.ExpoLinking;
const listeners = [];
globalThis.received = [];
globalThis.__rctDeviceEventEmitter = {
  addListener(eventName, listener) { listeners.push([eventName, listener]); return { remove() {} }; },
  emit(eventName, payload) { listeners.forEach(([name, listener]) => name === eventName && listener(payload)); },
};
const subscription = linking.onURLReceived((event) => globalThis.received.push(event.url));
let missingListener;
try { linking.onURLReceived(); } catch (error) { missingListener = error.message; }
globalThis.trace = JSON.stringify({
  methods: Object.keys(Object.getPrototypeOf(linking)).sort(),
  url: linking.getLinkingURL(),
  subscription: typeof subscription.remove,
  missingListener,
});
)JAVASCRIPT");

    reactHost.activation().onActivationUrlReceived("t3code://thread/1");

    EXPECT_EQ(beforeActivation, R"({"methods":["getLinkingURL","onURLReceived"],"url":null,"subscription":"function",)"
                                R"("missingListener":"ExpoLinking.onURLReceived takes a listener"})");
    EXPECT_EQ(settledTrace(reactHost, R"JAVASCRIPT(
globalThis.trace = JSON.stringify({
  url: globalThis.nativeModuleProxy.ExpoLinking.getLinkingURL(),
  received: globalThis.received,
});
)JAVASCRIPT"),
              R"({"url":"t3code://thread/1","received":["t3code://thread/1"]})");
}

/** Keeping the screen awake is not offered, so upstream's availability query is `false` and both calls reject. */
TEST(ExpoStartupModulesTest, KeepAwakeReportsItselfUnavailable) {
    EXPECT_EQ(
        settledTrace(R"JAVASCRIPT(
const keepAwake = globalThis.nativeModuleProxy.ExpoKeepAwake;
const settle = (promise) => promise.then((value) => value, (error) => `${error.code}: ${error.message}`);
(async () => {
  globalThis.trace = JSON.stringify({
    methods: Object.keys(Object.getPrototypeOf(keepAwake)).sort(),
    available: await keepAwake.isAvailableAsync(),
    activate: await settle(keepAwake.activate('tag')),
    deactivate: await settle(keepAwake.deactivate('tag')),
  });
})();
)JAVASCRIPT"),
        R"({"methods":["activate","deactivate","isAvailableAsync"],"available":false,)"
        R"("activate":"ERR_UNAVAILABLE: The method or property ExpoKeepAwake.activate is not available on linux",)"
        R"("deactivate":"ERR_UNAVAILABLE: The method or property ExpoKeepAwake.deactivate is not available )"
        R"(on linux"})");
}

/** No manifest is embedded yet, so the identity answers `null`, and nothing else is offered. */
TEST(ExpoStartupModulesTest, ApplicationAnswersAMissingIdentity) {
    EXPECT_EQ(settledTrace(R"JAVASCRIPT(
const application = globalThis.nativeModuleProxy.ExpoApplication;
globalThis.trace = JSON.stringify({
  methods: Object.keys(Object.getPrototypeOf(application)).sort(),
  constants: application.getConstants(),
});
)JAVASCRIPT"),
              R"({"methods":["getConstants"],"constants":{"applicationId":null,"applicationName":null,)"
              R"("nativeApplicationVersion":null,"nativeBuildVersion":null}})");
}

/** Without the portal the state is `UNKNOWN`, never a guessed connection, and a listener subscribes and removes. */
TEST(ExpoStartupModulesTest, NetworkAnswersAnUnknownStateAndSubscribes) {
    EXPECT_EQ(settledTrace(R"JAVASCRIPT(
const network = globalThis.nativeModuleProxy.ExpoNetwork;
(async () => {
  const subscription = network.onNetworkStateChanged(() => {});
  subscription.remove();
  globalThis.trace = JSON.stringify({
    methods: Object.keys(Object.getPrototypeOf(network)).sort(),
    state: await network.getNetworkStateAsync(),
    subscription: typeof subscription.remove,
  });
})();
)JAVASCRIPT"),
              R"({"methods":["getNetworkStateAsync"],"state":{"type":"UNKNOWN"},"subscription":"function"})");
}

/** Every constant comes from the machine itself; the shell reads the same sources as an independent oracle. */
TEST(ExpoStartupModulesTest, DeviceAnswersTheMachine) {
    std::array<char, HOST_NAME_MAX + 1> hostName{};
    utsname system{};

    static_cast<void>(gethostname(hostName.data(), hostName.size() - 1));
    static_cast<void>(uname(&system));

    const std::string osRelease = "{ . /etc/os-release 2>/dev/null || . /usr/lib/os-release; } && printf '%s' \"$";
    const std::string osName = shellOutputAsJson(osRelease + "NAME\"");

    EXPECT_EQ(settledTrace(R"JAVASCRIPT(
const device = globalThis.nativeModuleProxy.ExpoDevice;
(async () => {
  const constants = device.getConstants();
  const uptime = await device.getUptimeAsync();
  globalThis.trace = JSON.stringify({
    methods: Object.keys(Object.getPrototypeOf(device)).sort(),
    constants: { ...constants, totalMemory: constants.totalMemory > 0 },
    deviceType: await device.getDeviceTypeAsync(),
    uptime: uptime > 0,
    rooted: await device.isRootedExperimentalAsync(),
  });
})();
)JAVASCRIPT"),
              R"({"methods":["getConstants","getDeviceTypeAsync","getUptimeAsync","isRootedExperimentalAsync"],)"
              R"("constants":{"isDevice":true,"brand":null,"manufacturer":)" +
                  shellOutputAsJson("cat /sys/class/dmi/id/sys_vendor 2>/dev/null") +
                  R"(,"modelId":null,"modelName":)" +
                  shellOutputAsJson("cat /sys/class/dmi/id/product_name 2>/dev/null") +
                  R"(,"deviceYearClass":null,"totalMemory":true,"osName":)" +
                  (osName == "null" ? "\"" + std::string(system.sysname) + "\"" : osName) + R"(,"osVersion":)" +
                  shellOutputAsJson(osRelease + "VERSION_ID\"") + R"(,"osBuildId":)" +
                  shellOutputAsJson(osRelease + "BUILD_ID\"") + R"(,"osInternalBuildId":")" + system.release +
                  R"(","deviceName":")" + hostName.data() + R"(","deviceType":3,"supportedCpuArchitectures":[")" +
                  system.machine + R"("]},"deviceType":3,"uptime":true,"rooted":)" +
                  (geteuid() == 0 ? "true" : "false") + "}");
}

} // namespace
} // namespace react_native_linux
