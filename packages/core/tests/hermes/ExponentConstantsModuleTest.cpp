#include "ReactHost.h"

#include <array>
#include <chrono>
#include <climits>
#include <cxxreact/JSBigString.h>
#include <future>
#include <gtest/gtest.h>
#include <memory>
#include <string>
#include <unistd.h>

namespace react_native_linux {
namespace {

constexpr std::chrono::milliseconds kQuiescenceBudget{5000};
#ifdef NDEBUG
constexpr char kDebugMode[] = "false";
#else
constexpr char kDebugMode[] = "true";
#endif

constexpr char kExponentConstantsProbe[] = R"JAVASCRIPT(
const constantsModule = globalThis.nativeModuleProxy.ExponentConstants;
const constants = constantsModule.getConstants();
const uuidPattern = /^[\da-f]{8}-[\da-f]{4}-4[\da-f]{3}-[89ab][\da-f]{3}-[\da-f]{12}$/;

(async () => {
  globalThis.exponentConstantsTrace = JSON.stringify({
    methods: Object.keys(Object.getPrototypeOf(constantsModule)).sort(),
    constants: { ...constants, sessionId: uuidPattern.test(constants.sessionId) },
    sameSession: constantsModule.getConstants().sessionId === constants.sessionId,
    userAgent: await constantsModule.getWebViewUserAgentAsync(),
  });
})();
)JAVASCRIPT";

std::string hostName() {
    std::array<char, HOST_NAME_MAX + 1> name{};

    static_cast<void>(gethostname(name.data(), name.size() - 1));

    return name.data();
}

/**
 * ADR-0004: upstream's `expo-constants` reads `ExponentConstants` through `createTurboModuleToExpoProxy`, which
 * copies the module's own method names and `getConstants()`. Every field SDK 58's `NativeConstants` requires but
 * `name` is answered for a bare desktop app, `debugMode` is the build configuration, the session id is a v4 UUID that
 * stays fixed for the runtime, and the user agent of the web view this platform does not have resolves `null`.
 */
TEST(ExponentConstantsModuleTest, AnswersUpstreamExpoConstantsAsABareDesktopApp) {
    ReactHost reactHost;
    std::promise<std::string> trace;

    reactHost.loadScript(std::make_unique<facebook::react::JSBigStdString>(kExponentConstantsProbe),
                         "ExponentConstantsTest.js");
    EXPECT_TRUE(reactHost.runUntilQuiescent(kQuiescenceBudget));
    reactHost.reactInstance().getBufferedRuntimeExecutor()([&trace](facebook::jsi::Runtime& runtime) {
        const facebook::jsi::Value value = runtime.global().getProperty(runtime, "exponentConstantsTrace");

        trace.set_value(value.isString() ? value.getString(runtime).utf8(runtime) : "the probe never finished");
    });

    EXPECT_EQ(
        trace.get_future().get(),
        R"({"methods":["getConstants","getWebViewUserAgentAsync"],"constants":{"appOwnership":null,"debugMode":)" +
            std::string(kDebugMode) + R"(,"deviceName":")" + hostName() +
            R"(","deviceYearClass":null,"executionEnvironment":"bare","experienceUrl":"",)"
            R"("expoRuntimeVersion":null,"expoVersion":null,"isHeadless":false,"linkingUri":"",)"
            R"("manifest":null,"sessionId":true,"statusBarHeight":0,"systemFonts":[]},"sameSession":true,)"
            R"("userAgent":null})");
    EXPECT_FALSE(reactHost.hasReportedFatalError());
}

} // namespace
} // namespace react_native_linux
