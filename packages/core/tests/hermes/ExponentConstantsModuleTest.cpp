#include "ReactHost.h"

#include <array>
#include <chrono>
#include <climits>
#include <cxxreact/JSBigString.h>
#include <filesystem>
#include <fstream>
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

/**
 * The `manifest` `ExponentConstants` answers to a script loaded from `sourceUrl`, as JSON, with `assetsDirectory` set
 * as `--assets` sets it when it is not empty.
 */
std::string manifestOf(const std::string& sourceUrl, const std::string& assetsDirectory) {
    ReactHost reactHost;
    std::promise<std::string> trace;

    reactHost.setAssetsDirectory(assetsDirectory);
    reactHost.loadScript(std::make_unique<facebook::react::JSBigStdString>(
                             "globalThis.manifestTrace = "
                             "JSON.stringify(globalThis.nativeModuleProxy.ExponentConstants.getConstants().manifest);"),
                         sourceUrl);
    EXPECT_TRUE(reactHost.runUntilQuiescent(kQuiescenceBudget));
    reactHost.reactInstance().getBufferedRuntimeExecutor()([&trace](facebook::jsi::Runtime& runtime) {
        const facebook::jsi::Value value = runtime.global().getProperty(runtime, "manifestTrace");

        trace.set_value(value.isString() ? value.getString(runtime).utf8(runtime) : "the probe never finished");
    });

    return trace.get_future().get();
}

/**
 * #156, #696: the linux build writes the app config `@expo/config` evaluated as `app.config` in the application's
 * assets directory, and `manifest` serves it as the JSON string Android's module serves from its APK's
 * `assets/app.config`, which upstream's `Constants.ts` parses into `expoConfig`. The directory is `assets` beside a
 * bundle file, or the one `--assets` names, which is how a bundle a Metro dev server serves gets it. A source with
 * neither answers `null`.
 */
TEST(ExponentConstantsModuleTest, ServesTheAppConfigFromTheAssetsDirectory) {
    const std::filesystem::path application =
        std::filesystem::temp_directory_path() / ("rnl-expo-app-config-" + std::to_string(::getpid()));
    const std::string bundle = (application / "index.linux.bundle").string();
    const std::string devServerBundle = "http://127.0.0.1:8081/index.bundle?platform=linux&dev=true";
    const std::string appConfig = R"("{\"name\":\"probe\",\"scheme\":\"probe\"}")";

    std::filesystem::remove_all(application);
    std::filesystem::create_directories(application);
    const std::string withoutAppConfig = manifestOf(bundle, "");

    std::filesystem::create_directories(application / "assets");
    std::ofstream(application / "assets" / "app.config") << R"({"name":"probe","scheme":"probe"})";
    const std::string besideTheBundle = manifestOf(bundle, "");
    const std::string fromADevServer = manifestOf(devServerBundle, "");
    const std::string fromADevServerWithAssets = manifestOf(devServerBundle, (application / "assets").string());

    std::filesystem::remove_all(application);

    EXPECT_EQ(withoutAppConfig, "null");
    EXPECT_EQ(besideTheBundle, appConfig);
    EXPECT_EQ(fromADevServer, "null");
    EXPECT_EQ(fromADevServerWithAssets, appConfig);
}

} // namespace
} // namespace react_native_linux
