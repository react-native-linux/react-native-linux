#include "../LoopbackHttpServer.h"
#include "ExpoAsset.h"
#include "OnJavaScriptThread.h"

#include <cxxreact/JSBigString.h>
#include <filesystem>
#include <gtest/gtest.h>
#include <memory>
#include <string>
#include <unistd.h>

namespace react_native_linux {
namespace {

/** A loopback server whose `/missing` is a 404, `/stalled` never answers, and every other path the same asset. */
class ExpoAssetModuleTest : public ::testing::Test {
protected:
    void SetUp() override {
        directory_ = std::filesystem::temp_directory_path() / ("rnl-expo-asset-module-" + std::to_string(::getpid()));
    }

    void TearDown() override { std::filesystem::remove_all(directory_); }

    /**
     * Evaluates `expression` over `ExpoAsset` as upstream's `expo-asset` reaches it, and answers what it settled to
     * as JSON, or `threw: <message>`. `settle` turns one `downloadAsync` call into its URL or `rejected: <message>`.
     * The downloads settle off the JavaScript thread, so the host is pumped until the probe has written its trace.
     */
    std::string settleProbe(const std::string& expression) {
        ReactHost reactHost;
        const std::string probe = R"JAVASCRIPT(
const assetModule = globalThis.nativeModuleProxy.ExpoAsset;
const settle = (call) => assetModule.downloadAsync(...call).then((uri) => uri, (error) => `rejected: ${error.message}`);
Promise.resolve()
  .then(() => )JAVASCRIPT" + expression +
                                  R"JAVASCRIPT()
  .then((value) => JSON.stringify(value), (error) => `threw: ${error.message}`)
  .then((trace) => { globalThis.expoAssetTrace = trace; });
)JAVASCRIPT";

        reactHost.expoAssetDownloads().setDirectory(directory_);
        reactHost.loadScript(std::make_unique<facebook::react::JSBigStdString>(probe), "ExpoAssetTest.js");

        const std::string trace = awaitGlobalString(reactHost, "expoAssetTrace");

        EXPECT_FALSE(reactHost.hasReportedFatalError());

        return trace;
    }

    std::string fileUrl(const std::string& name) const { return "file://" + (directory_ / name).string(); }

    std::filesystem::path directory_;
    LoopbackHttpServer server_{[](const std::string& request, int socket) {
        if (!request.starts_with("GET /stalled ")) {
            respond(socket, request.starts_with("GET /missing ") ? "404 Not Found" : "200 OK", "", "asset bytes");
        }
    }};
};

/**
 * ADR-0004: upstream's `Asset.downloadAsync` calls `ExpoAsset.downloadAsync(uri, hash, type)`, and the promise it
 * returns settles with the local URL, or rejects with upstream's message (`ExpoAssetTest.cpp` covers the rest).
 */
TEST_F(ExpoAssetModuleTest, SettlesUpstreamsDownloadAsync) {
    const std::string missing = server_.url("/missing");

    EXPECT_EQ(settleProbe("Promise.all([['file:///proc/self/comm', null, ''], ['" + server_.url("/hashed.png") +
                          "', '0123abcd', 'png'], ['" + missing + "', null, 'txt']].map(settle))"),
              R"(["file:///proc/self/comm",")" + fileUrl("ExponentAsset-0123abcd.png") +
                  R"(","rejected: Unable to download asset from url: ')" + missing + R"('"])");
}

/** A stalled download leaves the JavaScript thread free: a timer set after it still fires first. */
TEST_F(ExpoAssetModuleTest, AStalledDownloadDoesNotBlockTheJavaScriptThread) {
    EXPECT_EQ(settleProbe("Promise.race([settle(['" + server_.url("/stalled") +
                          "', null, 'bin']), new Promise((resolve) => setTimeout(() => resolve('timer fired'), 10))])"),
              R"("timer fired")");
}

TEST_F(ExpoAssetModuleTest, ThrowsWithoutUpstreamsThreeArguments) {
    EXPECT_EQ(settleProbe("assetModule.downloadAsync('file:///x')"),
              "threw: ExpoAsset.downloadAsync takes a url, an md5Hash and a type");
}

} // namespace
} // namespace react_native_linux
