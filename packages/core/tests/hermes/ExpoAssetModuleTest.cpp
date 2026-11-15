#include "../LoopbackHttpServer.h"
#include "ReactHost.h"

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <cxxreact/JSBigString.h>
#include <filesystem>
#include <fstream>
#include <functional>
#include <future>
#include <gtest/gtest.h>
#include <iterator>
#include <memory>
#include <optional>
#include <string>
#include <unistd.h>

namespace react_native_linux {
namespace {

constexpr std::chrono::milliseconds kQuiescenceBudget{5000};

/**
 * Evaluates `expression` over `ExpoAsset` as upstream's `expo-asset` reaches it, and answers what it settled to as
 * JSON, or `threw: <message>`. `settle` turns one `downloadAsync` call into its URL or `rejected: <message>`.
 */
std::string settleProbe(const std::string& expression) {
    ReactHost reactHost;
    std::promise<std::string> trace;
    const std::string probe = R"JAVASCRIPT(
const assetModule = globalThis.nativeModuleProxy.ExpoAsset;
const settle = (call) => assetModule.downloadAsync(...call).then((uri) => uri, (error) => `rejected: ${error.message}`);
Promise.resolve()
  .then(() => )JAVASCRIPT" + expression +
                              R"JAVASCRIPT()
  .then((value) => JSON.stringify(value), (error) => `threw: ${error.message}`)
  .then((trace) => { globalThis.expoAssetTrace = trace; });
)JAVASCRIPT";

    reactHost.loadScript(std::make_unique<facebook::react::JSBigStdString>(probe), "ExpoAssetTest.js");
    EXPECT_TRUE(reactHost.runUntilQuiescent(kQuiescenceBudget));
    reactHost.reactInstance().getBufferedRuntimeExecutor()([&trace](facebook::jsi::Runtime& runtime) {
        const facebook::jsi::Value value = runtime.global().getProperty(runtime, "expoAssetTrace");

        trace.set_value(value.isString() ? value.getString(runtime).utf8(runtime) : "the probe never finished");
    });
    EXPECT_FALSE(reactHost.hasReportedFatalError());

    return trace.get_future().get();
}

std::string readFile(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);

    return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
}

std::string urlHash(const std::string& url) { return std::to_string(std::hash<std::string>{}(url)); }

/** A loopback server whose `/missing` is a 404 and every other path the same asset, counting what it serves. */
class ExpoAssetModuleTest : public ::testing::Test {
protected:
    void SetUp() override {
        scratch_ = std::filesystem::temp_directory_path() / ("rnl-expo-asset-" + std::to_string(::getpid()));
        std::filesystem::create_directories(scratch_);
        savedHome_ = environment("HOME");
        savedCacheHome_ = environment("XDG_CACHE_HOME");
    }

    void TearDown() override {
        restore("HOME", savedHome_);
        restore("XDG_CACHE_HOME", savedCacheHome_);
        std::filesystem::remove_all(scratch_);
    }

    static std::optional<std::string> environment(const char* name) {
        const char* value = std::getenv(name);

        return value == nullptr ? std::nullopt : std::optional<std::string>(value);
    }

    static void restore(const char* name, const std::optional<std::string>& value) {
        if (value.has_value()) {
            ::setenv(name, value->c_str(), 1);
        } else {
            ::unsetenv(name);
        }
    }

    std::filesystem::path scratch_;
    std::atomic<int> served_{0};
    LoopbackHttpServer server_{[this](const std::string& request, int socket) {
        served_.fetch_add(1);
        respond(socket, request.starts_with("GET /missing ") ? "404 Not Found" : "200 OK", "", "asset bytes");
    }};

private:
    std::optional<std::string> savedHome_;
    std::optional<std::string> savedCacheHome_;
};

/**
 * ADR-0004: upstream's `Asset.downloadAsync` calls `ExpoAsset.downloadAsync(uri, hash, type)`. A `file://` URL is
 * already local; a remote one is downloaded once into `$XDG_CACHE_HOME`, named after its hash, or after a hash of its
 * URL when it has none, and reused from there; a failed download rejects with upstream's message.
 */
TEST_F(ExpoAssetModuleTest, DownloadsARemoteAssetOnceIntoTheXdgCache) {
    ::setenv("XDG_CACHE_HOME", scratch_.c_str(), 1);
    const std::string hashed = server_.url("/hashed.png");
    const std::string unhashed = server_.url("/unhashed.ttf");
    const std::string missing = server_.url("/missing");
    const std::filesystem::path cache = scratch_ / "react-native-linux" / "expo-asset";

    EXPECT_EQ(settleProbe("Promise.all([['file:///proc/self/comm', null, ''], ['" + hashed +
                          "', '0123abcd', 'png'], ['" + hashed + "', '0123abcd', 'png'], ['" + unhashed +
                          "', null, 'ttf'], ['" + missing + "', null, 'txt']].map(settle))"),
              R"([")"
              "file:///proc/self/comm"
              R"(",")" +
                  ("file://" + (cache / "ExponentAsset-0123abcd.png").string()) + R"(",")" +
                  ("file://" + (cache / "ExponentAsset-0123abcd.png").string()) + R"(",")" +
                  ("file://" + (cache / ("ExponentAsset-" + urlHash(unhashed) + ".ttf")).string()) +
                  R"(","rejected: Unable to download asset from url: ')" + missing + R"('"])");
    EXPECT_EQ(served_.load(), 3);
    EXPECT_EQ(readFile(cache / "ExponentAsset-0123abcd.png"), "asset bytes");
}

/** `$XDG_CACHE_HOME` counts only when absolute; otherwise the cache is `$HOME/.cache`, as the XDG spec asks. */
TEST_F(ExpoAssetModuleTest, FallsBackToTheHomeCacheWithoutAnAbsoluteXdgCacheHome) {
    ::setenv("XDG_CACHE_HOME", "relative/cache", 1);
    ::setenv("HOME", scratch_.c_str(), 1);
    const std::string asset = server_.url("/font.ttf");

    EXPECT_EQ(settleProbe("Promise.all([['" + asset + "', 'feed', 'ttf']].map(settle))"),
              R"([")" +
                  ("file://" + (scratch_ / ".cache/react-native-linux/expo-asset/ExponentAsset-feed.ttf").string()) +
                  R"("])");

    ::unsetenv("XDG_CACHE_HOME");
    ::unsetenv("HOME");
    const std::string missing = server_.url("/missing");

    EXPECT_EQ(settleProbe("Promise.all([['" + missing + "', null, 'ttf']].map(settle))"),
              R"(["rejected: Unable to download asset from url: ')" + missing + R"('"])");
}

TEST_F(ExpoAssetModuleTest, RejectsWhenTheCacheCannotBeWritten) {
    const std::filesystem::path notADirectory = scratch_ / "file";

    std::ofstream(notADirectory) << "occupied";
    ::setenv("XDG_CACHE_HOME", notADirectory.c_str(), 1);

    EXPECT_EQ(settleProbe("Promise.all([['" + server_.url("/image.png") + "', 'beef', 'png']].map(settle))"),
              R"(["rejected: Unable to save asset to directory: 'file://)" +
                  (notADirectory / "react-native-linux" / "expo-asset").string() + R"('"])");
}

TEST_F(ExpoAssetModuleTest, ThrowsWithoutUpstreamsThreeArguments) {
    EXPECT_EQ(settleProbe("assetModule.downloadAsync('file:///x')"),
              "threw: ExpoAsset.downloadAsync takes a url, an md5Hash and a type");
}

} // namespace
} // namespace react_native_linux
