#include "Activation.h"
#include "ExpoDeviceModule.h"
#include "OnJavaScriptThread.h"
#include "ReactHost.h"

#include <ReactCommon/TurboModule.h>
#include <array>
#include <chrono>
#include <climits>
#include <cstdio>
#include <cxxreact/JSBigString.h>
#include <filesystem>
#include <folly/dynamic.h>
#include <folly/json.h>
#include <fstream>
#include <gtest/gtest.h>
#include <hermes/hermes.h>
#include <memory>
#include <string>
#include <sys/utsname.h>
#include <unistd.h>
#include <vector>

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

/** What a shell prints for `command`, without its trailing newline, or `null` when it is empty. */
folly::dynamic shellOutput(const std::string& command) {
    std::array<char, PATH_MAX> buffer{};
    std::string output;
    const std::unique_ptr<FILE, decltype(&pclose)> pipe(popen(command.c_str(), "r"), &pclose);

    while (fgets(buffer.data(), buffer.size(), pipe.get()) != nullptr) {
        output += buffer.data();
    }

    if (!output.empty() && output.back() == '\n') {
        output.pop_back();
    }

    return output.empty() ? folly::dynamic(nullptr) : folly::dynamic(output);
}

/** `ExpoDevice.getConstants()` over `dmiRoot` and `osReleaseRoots`, through `JSON.stringify` and back. */
folly::dynamic deviceConstants(const std::filesystem::path& dmiRoot,
                               const std::vector<std::filesystem::path>& osReleaseRoots) {
    const std::shared_ptr<facebook::jsi::Runtime> runtime = facebook::hermes::makeHermesRuntime();
    const std::shared_ptr<facebook::react::TurboModule> device = makeExpoDeviceModule(nullptr, dmiRoot, osReleaseRoots);
    const facebook::jsi::Value constants =
        device->get(*runtime, facebook::jsi::PropNameID::forAscii(*runtime, "getConstants"))
            .asObject(*runtime)
            .asFunction(*runtime)
            .call(*runtime);
    const facebook::jsi::Value json = runtime->global()
                                          .getPropertyAsObject(*runtime, "JSON")
                                          .getPropertyAsFunction(*runtime, "stringify")
                                          .call(*runtime, constants);

    return folly::parseJson(json.getString(*runtime).utf8(*runtime));
}

/** A scratch directory of `dmi/` and `etc/`, `usr/lib/` os-release roots, removed with the fixture. */
class ExpoStartupModulesDeviceTest : public ::testing::Test {
protected:
    void SetUp() override { std::filesystem::create_directories(root_ / "dmi"); }

    void TearDown() override { std::filesystem::remove_all(root_); }

    void write(const std::filesystem::path& relativePath, const std::string& contents) const {
        std::filesystem::create_directories((root_ / relativePath).parent_path());
        std::ofstream(root_ / relativePath) << contents;
    }

    [[nodiscard]] folly::dynamic constants() const {
        return deviceConstants(root_ / "dmi", {root_ / "etc", root_ / "usr/lib"});
    }

private:
    std::filesystem::path root_ =
        std::filesystem::temp_directory_path() / ("rnl-expo-device-" + std::to_string(::getpid()));
};

/** Firmware without a DMI table answers `null`, never a guess, and a missing `/etc/os-release` falls back. */
TEST_F(ExpoStartupModulesDeviceTest, MissingDmiIsNullAndOsReleaseFallsBackToUsrLib) {
    write("usr/lib/os-release", "ID=arch\nNAME='Arch Linux'\nVERSION_ID=\"2026.11\"\nBUILD_ID=rolling\n");

    const folly::dynamic answered = constants();

    EXPECT_EQ(answered["manufacturer"], nullptr);
    EXPECT_EQ(answered["modelName"], nullptr);
    EXPECT_EQ(answered["osName"], "Arch Linux");
    EXPECT_EQ(answered["osVersion"], "2026.11");
    EXPECT_EQ(answered["osBuildId"], "rolling");
}

/** `/etc/os-release` wins over `/usr/lib/os-release`; an empty value is `null`, so `osName` falls back to `uname`. */
TEST_F(ExpoStartupModulesDeviceTest, EmptyOsReleaseValuesAreNull) {
    utsname system{};

    static_cast<void>(uname(&system));
    write("etc/os-release", "NAME=\"\"\nVERSION_ID=\nBUILD_ID=''\n");
    write("usr/lib/os-release", "NAME=Fallback\nVERSION_ID=1\nBUILD_ID=1\n");

    const folly::dynamic answered = constants();

    EXPECT_EQ(answered["osName"], system.sysname);
    EXPECT_EQ(answered["osVersion"], nullptr);
    EXPECT_EQ(answered["osBuildId"], nullptr);
}

/** DMI strings reach JavaScript verbatim, a quote and a backslash included. */
TEST_F(ExpoStartupModulesDeviceTest, DmiStringsKeepQuotesAndBackslashes) {
    write("dmi/sys_vendor", "Vendor \"Q\" \\ Co\n");
    write("dmi/product_name", "C:\\Model \"X\"\n");

    const folly::dynamic answered = constants();

    EXPECT_EQ(answered["manufacturer"], "Vendor \"Q\" \\ Co");
    EXPECT_EQ(answered["modelName"], "C:\\Model \"X\"");
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

    const std::string osRelease = "{ unset NAME VERSION_ID BUILD_ID; "
                                  ". /etc/os-release 2>/dev/null || . /usr/lib/os-release; } && printf '%s' \"$";
    const folly::dynamic osName = shellOutput(osRelease + "NAME\"");
    const folly::dynamic expected = folly::dynamic::object(
        "methods",
        folly::dynamic::array("getConstants", "getDeviceTypeAsync", "getUptimeAsync", "isRootedExperimentalAsync"))(
        "constants",
        folly::dynamic::object("isDevice", true)("brand", nullptr)(
            "manufacturer", shellOutput("cat /sys/class/dmi/id/sys_vendor 2>/dev/null"))("modelId", nullptr)(
            "modelName", shellOutput("cat /sys/class/dmi/id/product_name 2>/dev/null"))("deviceYearClass", nullptr)(
            "totalMemory", true)("osName", osName.isNull() ? folly::dynamic(system.sysname) : osName)(
            "osVersion", shellOutput(osRelease + "VERSION_ID\""))("osBuildId", shellOutput(osRelease + "BUILD_ID\""))(
            "osInternalBuildId", system.release)("deviceName", hostName.data())("deviceType", 3)(
            "supportedCpuArchitectures", folly::dynamic::array(system.machine)))("deviceType", 3)("uptime", true)(
        "rooted", geteuid() == 0);

    EXPECT_EQ(folly::parseJson(settledTrace(R"JAVASCRIPT(
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
)JAVASCRIPT")),
              expected);
}

} // namespace
} // namespace react_native_linux
