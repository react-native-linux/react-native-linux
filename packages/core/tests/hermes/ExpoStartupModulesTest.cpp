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

/** `device.getConstants()`, through `JSON.stringify` and back. */
folly::dynamic deviceConstants(facebook::react::TurboModule& device) {
    const std::shared_ptr<facebook::jsi::Runtime> runtime = facebook::hermes::makeHermesRuntime();
    const facebook::jsi::Value constants =
        device.get(*runtime, facebook::jsi::PropNameID::forAscii(*runtime, "getConstants"))
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

    [[nodiscard]] std::shared_ptr<facebook::react::TurboModule> device() const {
        return makeExpoDeviceModule(nullptr, root_ / "dmi", {root_ / "etc", root_ / "usr/lib"});
    }

    [[nodiscard]] folly::dynamic constants() const { return deviceConstants(*device()); }

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

/** Values decode as the shell that sources os-release reads them, without running one. */
TEST_F(ExpoStartupModulesDeviceTest, OsReleaseValuesDecodeTheirQuoting) {
    write("etc/os-release", "NAME=\"Acme \\\"Linux\\\" \\\\ \\$HOME \\`id\\` C:\\x\"\n"
                            "VERSION_ID='$(id) \\\" literal'\nBUILD_ID=\"trailing\\\"\n");

    const folly::dynamic answered = constants();

    EXPECT_EQ(answered["osName"], "Acme \"Linux\" \\ $HOME `id` C:\\x");
    EXPECT_EQ(answered["osVersion"], "$(id) \\\" literal");
    EXPECT_EQ(answered["osBuildId"], "trailing\\");
}

/** A value whose ends are not one matching pair of quotes is taken as written. */
TEST_F(ExpoStartupModulesDeviceTest, OsReleaseValuesWithoutMatchingQuotesAreLiteral) {
    write("etc/os-release", "NAME=\"Half\nVERSION_ID=2.2\nBUILD_ID='mixed\"\n");

    const folly::dynamic answered = constants();

    EXPECT_EQ(answered["osName"], "\"Half");
    EXPECT_EQ(answered["osVersion"], "2.2");
    EXPECT_EQ(answered["osBuildId"], "'mixed\"");
}

/** The machine is read once, as the module is made, and `getConstants` serializes that reading however often it runs.
 */
TEST_F(ExpoStartupModulesDeviceTest, TheMachineIsReadOnceAsTheModuleIsMade) {
    write("dmi/sys_vendor", "Before\n");
    write("etc/os-release", "NAME=Before\n");

    const std::shared_ptr<facebook::react::TurboModule> madeBefore = device();

    write("dmi/sys_vendor", "After\n");
    write("etc/os-release", "NAME=After\n");

    const folly::dynamic first = deviceConstants(*madeBefore);

    EXPECT_EQ(first["manufacturer"], "Before");
    EXPECT_EQ(first["osName"], "Before");
    EXPECT_EQ(deviceConstants(*madeBefore), first);
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

    const std::string osRelease = "unset NAME VERSION_ID BUILD_ID; if [ -r /etc/os-release ]; then . /etc/os-release; "
                                  "else . /usr/lib/os-release; fi && printf '%s' \"$";
    const folly::dynamic osName = shellOutput(osRelease + "NAME\"");
    const folly::dynamic expected = folly::dynamic::object("methods", folly::dynamic::array("getConstants"))(
        "constants",
        folly::dynamic::object("isDevice", true)("brand", nullptr)(
            "manufacturer", shellOutput("cat /sys/class/dmi/id/sys_vendor 2>/dev/null"))("modelId", nullptr)(
            "modelName", shellOutput("cat /sys/class/dmi/id/product_name 2>/dev/null"))("deviceYearClass", nullptr)(
            "totalMemory", true)("osName", osName.isNull() ? folly::dynamic(system.sysname) : osName)(
            "osVersion", shellOutput(osRelease + "VERSION_ID\""))("osBuildId", shellOutput(osRelease + "BUILD_ID\""))(
            "osInternalBuildId", system.release)("deviceName", hostName.data())("deviceType", 3)(
            "supportedCpuArchitectures", folly::dynamic::array(system.machine)));

    EXPECT_EQ(folly::parseJson(settledTrace(R"JAVASCRIPT(
const device = globalThis.nativeModuleProxy.ExpoDevice;
const constants = device.getConstants();
globalThis.trace = JSON.stringify({
  methods: Object.keys(Object.getPrototypeOf(device)).sort(),
  constants: { ...constants, totalMemory: constants.totalMemory > 0 },
});
)JAVASCRIPT")),
              expected);
}

/**
 * #276: `ExpoCrypto` answers the calls upstream's `expo-crypto` makes for T3 Code, through the module registry
 * `requireNativeModule` falls back to. `digest` honours the input view's byte offset and, as Android does, writes at
 * most the output's length; `digestStringAsync` defaults to hex and rejects an algorithm it has not; and
 * `getRandomValues` fills exactly the view it is given and returns it.
 */
TEST(ExpoStartupModulesTest, CryptoAnswersDigestsRandomBytesAndUuids) {
    EXPECT_EQ(settledTrace(R"JAVASCRIPT(
const crypto = globalThis.nativeModuleProxy.ExpoCrypto;
const hex = (bytes) => Array.from(bytes, (byte) => byte.toString(16).padStart(2, '0')).join('');
const rejection = (promise) => promise.then(() => 'resolved', (error) => error.message);
const padded = new Uint8Array([0, 0x61, 0x62, 0x63, 0]);
const sha256 = new Uint8Array(32);
const truncated = new Uint8Array(4);
const random = new Uint8Array(40);
const window = random.subarray(4, 36);

crypto.digest('SHA-256', sha256, padded.subarray(1, 4));
crypto.digest('SHA-1', truncated, padded.subarray(1, 4));
globalThis.trace = [
  Object.keys(Object.getPrototypeOf(crypto)).sort().join(','),
  hex(sha256),
  hex(truncated),
  await crypto.digestStringAsync('MD5', 'abc'),
  await crypto.digestStringAsync('SHA-256', 'abc', { encoding: 'base64' }),
  await rejection(crypto.digestStringAsync('MD4', 'abc', { encoding: 'hex' })),
  await rejection(crypto.digestStringAsync('SHA-256', 'abc', { encoding: 'base32' })),
  crypto.getRandomValues(window) === window,
  window.some((byte) => byte !== 0),
  random.subarray(0, 4).every((byte) => byte === 0) && random.subarray(36).every((byte) => byte === 0),
  /^[0-9a-f]{8}-[0-9a-f]{4}-4[0-9a-f]{3}-[89ab][0-9a-f]{3}-[0-9a-f]{12}$/.test(crypto.randomUUID()),
  crypto.randomUUID() !== crypto.randomUUID(),
].join(' | ');
)JAVASCRIPT"),
              "digest,digestStringAsync,getRandomValues,randomUUID"
              " | ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"
              " | a9993e36"
              " | 900150983cd24fb0d6963f7d28e17f72"
              " | ungWv48Bz+pBQUDeXa4iI7ADYaOWF3qctBD/YfIAFa0="
              " | expo-crypto on Linux has no digest algorithm 'MD4'"
              " | expo-crypto has no digest encoding 'base32'"
              " | true | true | true | true | true");
}

/** A call without the arguments it needs throws to JavaScript, never reads past them. */
TEST(ExpoStartupModulesTest, CryptoThrowsForMissingArguments) {
    EXPECT_EQ(settledTrace(R"JAVASCRIPT(
const crypto = globalThis.nativeModuleProxy.ExpoCrypto;
const thrown = (call) => { try { call(); return 'returned'; } catch (error) { return error.message.includes('missing argument'); } };
globalThis.trace = [
  thrown(() => crypto.digest('SHA-256', new Uint8Array(32))),
  thrown(() => crypto.getRandomValues()),
  await crypto.digestStringAsync('SHA-256').then(() => 'resolved', (error) => error.message),
].join(' | ');
)JAVASCRIPT"),
              "true | true | ExpoCrypto: missing argument 1");
}

/**
 * `expo-crypto`'s entry extends `ExpoCryptoAES`'s `EncryptionKey` and `SealedData` as it is imported, so both are
 * `SharedObject` classes a subclass can extend; T3 Code calls no AES, so constructing one throws and the module has
 * no methods.
 */
TEST(ExpoStartupModulesTest, CryptoAesOffersOnlyTheClassesTheEntryExtends) {
    EXPECT_EQ(settledTrace(R"JAVASCRIPT(
globalThis.expo = { SharedObject: class SharedObject {} };
const aes = globalThis.nativeModuleProxy.ExpoCryptoAES;
const { EncryptionKey, SealedData } = aes.getConstants();
class AESEncryptionKey extends EncryptionKey {}
class AESSealedData extends SealedData {}
const constructed = (Class) => { try { new Class(); return 'constructed'; } catch (error) { return error.message; } };
globalThis.trace = [
  Object.keys(Object.getPrototypeOf(aes)).sort().join(','),
  EncryptionKey.prototype instanceof globalThis.expo.SharedObject,
  constructed(AESEncryptionKey).includes("expo-crypto's AES is not available on Linux"),
  constructed(AESSealedData).includes("expo-crypto's AES is not available on Linux"),
].join(' | ');
)JAVASCRIPT"),
              "getConstants | true | true | true");
}

} // namespace
} // namespace react_native_linux
