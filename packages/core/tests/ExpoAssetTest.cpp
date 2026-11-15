#include "ExpoAsset.h"

#include "LoopbackHttpServer.h"

#include <atomic>
#include <chrono>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <functional>
#include <future>
#include <gtest/gtest.h>
#include <memory>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <unistd.h>
#include <vector>

namespace react_native_linux {
namespace {

constexpr std::chrono::seconds kSettleBudget{10};
constexpr std::string_view kAssetMd5 = "1df8978624f56fb1c8290351ca43b3d0";

std::string readFile(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    std::ostringstream contents;

    contents << stream.rdbuf();

    return contents.str();
}

std::string urlHash(const std::string& url) { return std::to_string(std::hash<std::string>{}(url)); }

std::vector<std::string> directoryEntries(const std::filesystem::path& directory) {
    std::vector<std::string> names;

    for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator(directory)) {
        names.push_back(entry.path().filename().string());
    }

    return names;
}

/**
 * A loopback server whose `/missing` is a 404, `/moved` a 302 to `/asset`, `/stalled` never answers, and every other
 * path the same asset, counting what it serves.
 */
class ExpoAssetTest : public ::testing::Test {
protected:
    void SetUp() override {
        directory_ = std::filesystem::temp_directory_path() / ("rnl-expo-asset-" + std::to_string(::getpid()));
        std::filesystem::remove_all(directory_);
        downloads_.setDirectory(directory_);
    }

    void TearDown() override { std::filesystem::remove_all(directory_); }

    /** One `download`, as the URL it resolved or `rejected: <message>`. */
    std::string settle(const std::string& url, const std::optional<std::string>& md5Hash, const std::string& type) {
        const auto settled = std::make_shared<std::promise<std::string>>();
        std::future<std::string> result = settled->get_future();

        downloads_.download(
            url, md5Hash, type, [settled](const std::string& fileUrl) { settled->set_value(fileUrl); },
            [settled](const std::string& message) { settled->set_value("rejected: " + message); });

        return result.wait_for(kSettleBudget) == std::future_status::ready ? result.get() : "never settled";
    }

    std::string fileUrl(const std::string& name) const { return "file://" + (directory_ / name).string(); }

    std::filesystem::path directory_;
    std::atomic<int> served_{0};
    LoopbackHttpServer server_{[this](const std::string& request, int socket) {
        if (request.starts_with("GET /stalled ")) {
            return;
        }

        served_.fetch_add(1);

        if (request.starts_with("GET /missing ")) {
            respond(socket, "404 Not Found", "", "");
        } else if (request.starts_with("GET /moved ")) {
            respond(socket, "302 Found", "Location: /asset\r\n", "");
        } else {
            respond(socket, "200 OK", "", "asset bytes");
        }
    }};
    ExpoAssetDownloads downloads_;
};

/**
 * ADR-0004: a `file://` URL is already local; a remote one is downloaded once, named after its hash, or after a hash
 * of its URL when it has none, and reused from there; a failed download rejects with upstream's message.
 */
TEST_F(ExpoAssetTest, DownloadsARemoteAssetOnceAndReusesIt) {
    const std::string hashed = server_.url("/hashed.png");
    const std::string unhashed = server_.url("/unhashed.ttf");
    const std::string hashedName = "ExponentAsset-" + std::string(kAssetMd5) + ".png";

    EXPECT_EQ(settle("file:///proc/self/comm", std::nullopt, ""), "file:///proc/self/comm");
    EXPECT_EQ(settle(hashed, std::string(kAssetMd5), "png"), fileUrl(hashedName));
    EXPECT_EQ(settle(hashed, std::string(kAssetMd5), "png"), fileUrl(hashedName));
    EXPECT_EQ(settle(unhashed, std::nullopt, "ttf"), fileUrl("ExponentAsset-" + urlHash(unhashed) + ".ttf"));
    EXPECT_EQ(settle(unhashed, std::nullopt, "ttf"), fileUrl("ExponentAsset-" + urlHash(unhashed) + ".ttf"));
    EXPECT_EQ(served_.load(), 2);
    EXPECT_EQ(readFile(directory_ / hashedName), "asset bytes");
}

/**
 * As upstream's iOS and Android modules do, a cached file whose contents no longer match its MD5, say one a crash
 * truncated, is downloaded again, while one without an MD5 is reused as it is.
 */
TEST_F(ExpoAssetTest, DownloadsAgainWhenTheCachedFileNoLongerMatchesItsMd5) {
    const std::string unhashed = server_.url("/unhashed.ttf");
    const std::filesystem::path hashedPath = directory_ / ("ExponentAsset-" + std::string(kAssetMd5) + ".png");
    const std::filesystem::path unhashedPath = directory_ / ("ExponentAsset-" + urlHash(unhashed) + ".ttf");

    std::filesystem::create_directories(directory_);
    std::ofstream(hashedPath) << "truncated";
    std::ofstream(unhashedPath) << "stale";

    EXPECT_EQ(settle(server_.url("/asset.png"), std::string(kAssetMd5), "png"), "file://" + hashedPath.string());
    EXPECT_EQ(readFile(hashedPath), "asset bytes");
    EXPECT_EQ(settle(unhashed, std::nullopt, "ttf"), "file://" + unhashedPath.string());
    EXPECT_EQ(readFile(unhashedPath), "stale");
    EXPECT_EQ(served_.load(), 1);
}

/** A directory where the cached file belongs is no cache hit, and its publish rejects instead of throwing. */
TEST_F(ExpoAssetTest, RejectsWhenADirectoryOccupiesTheCachedPath) {
    const std::filesystem::path hashedPath = directory_ / ("ExponentAsset-" + std::string(kAssetMd5) + ".png");

    std::filesystem::create_directories(hashedPath);

    EXPECT_EQ(settle(server_.url("/asset.png"), std::string(kAssetMd5), "png"),
              "rejected: Unable to save asset to directory: 'file://" + directory_.string() + "'");
}

/** Checking and hashing a cached file is disk work, so even a cache hit settles off the calling thread. */
TEST_F(ExpoAssetTest, SettlesACachedAssetOffTheCallingThread) {
    const auto settledOn = std::make_shared<std::promise<std::thread::id>>();
    std::future<std::thread::id> settled = settledOn->get_future();

    std::filesystem::create_directories(directory_);
    std::ofstream(directory_ / ("ExponentAsset-" + std::string(kAssetMd5) + ".bin")) << "asset bytes";
    downloads_.download(
        server_.url("/cached.bin"), std::string(kAssetMd5), "bin",
        [settledOn](const std::string&) { settledOn->set_value(std::this_thread::get_id()); },
        [settledOn](const std::string&) { settledOn->set_value(std::this_thread::get_id()); });

    ASSERT_EQ(settled.wait_for(kSettleBudget), std::future_status::ready);
    EXPECT_NE(settled.get(), std::this_thread::get_id());
    EXPECT_EQ(served_.load(), 0);
}

TEST_F(ExpoAssetTest, RejectsAFailedDownload) {
    const std::string missing = server_.url("/missing");
    const std::string unreachable = "http://127.0.0.1:1/asset";

    EXPECT_EQ(settle(missing, std::nullopt, "txt"), "rejected: Unable to download asset from url: '" + missing + "'");
    EXPECT_EQ(settle(unreachable, std::nullopt, "txt"),
              "rejected: Unable to download asset from url: '" + unreachable + "'");
    EXPECT_FALSE(std::filesystem::exists(directory_));
}

/** A dev server or CDN that redirects still yields the asset, as upstream's `NSURLSession` follows it. */
TEST_F(ExpoAssetTest, FollowsARedirect) {
    EXPECT_EQ(settle(server_.url("/moved"), "feed", "png"), fileUrl("ExponentAsset-feed.png"));
    EXPECT_EQ(readFile(directory_ / "ExponentAsset-feed.png"), "asset bytes");
}

/** A stalled server holds only its own download: the caller's thread never waits on the network. */
TEST_F(ExpoAssetTest, ReturnsBeforeARemoteDownloadSettles) {
    const auto settled = std::make_shared<std::atomic<bool>>(false);

    downloads_.download(
        server_.url("/stalled"), std::nullopt, "bin", [settled](const std::string&) { settled->store(true); },
        [settled](const std::string&) { settled->store(true); });

    EXPECT_EQ(settle(server_.url("/after-stall"), "cafe", "bin"), fileUrl("ExponentAsset-cafe.bin"));
    EXPECT_FALSE(settled->load());
}

/** A `data:` URL is decoded here rather than handed to the HTTP client, and saved like a download. */
TEST_F(ExpoAssetTest, DecodesABase64DataUrl) {
    const std::string encoded = "data:font/ttf;base64,ZGF0YSBieXRlcw==";
    const std::string plain = "data:text/plain,plain";
    const std::string malformed = "data:text/plain;base64,!!!!";

    EXPECT_EQ(settle(encoded, std::nullopt, "ttf"), fileUrl("ExponentAsset-" + urlHash(encoded) + ".ttf"));
    EXPECT_EQ(readFile(directory_ / ("ExponentAsset-" + urlHash(encoded) + ".ttf")), "data bytes");
    EXPECT_EQ(settle(plain, std::nullopt, "txt"), "rejected: Unable to download asset from url: '" + plain + "'");
    EXPECT_EQ(settle("data:no-comma", std::nullopt, "txt"),
              "rejected: Unable to download asset from url: 'data:no-comma'");
    EXPECT_EQ(settle(malformed, std::nullopt, "txt"),
              "rejected: Unable to download asset from url: '" + malformed + "'");
    EXPECT_EQ(served_.load(), 0);
}

TEST_F(ExpoAssetTest, RejectsWhenTheCacheCannotBeWritten) {
    const std::filesystem::path notADirectory = directory_ / "file";

    std::filesystem::create_directories(directory_);
    std::ofstream(notADirectory) << "occupied";
    downloads_.setDirectory(notADirectory / "expo-asset");

    EXPECT_EQ(settle(server_.url("/image.png"), "beef", "png"),
              "rejected: Unable to save asset to directory: 'file://" + (notADirectory / "expo-asset").string() + "'");

    downloads_.setDirectory({});

    EXPECT_EQ(settle(server_.url("/image.png"), "beef", "png"), "rejected: Unable to save asset to directory: ''");
}

/**
 * Writers in one process or several publish to one path through temporary files of their own, so each publish
 * succeeds and a reader only ever sees one writer's whole file.
 */
TEST_F(ExpoAssetTest, ConcurrentWritersNeverCorruptOneAnother) {
    constexpr size_t kWriterCount = 4;
    constexpr size_t kPublishesPerWriter = 25;
    constexpr size_t kPayloadBytes = 64 * 1024;
    const std::filesystem::path localPath = directory_ / "ExponentAsset-shared.bin";
    std::vector<std::string> payloads;
    std::atomic<size_t> failures{0};
    std::vector<std::thread> writers;

    for (size_t writer = 0; writer < kWriterCount; ++writer) {
        payloads.emplace_back(kPayloadBytes, static_cast<char>('a' + writer));
    }

    for (size_t writer = 0; writer < kWriterCount; ++writer) {
        writers.emplace_back([&, writer]() {
            for (size_t publish = 0; publish < kPublishesPerWriter; ++publish) {
                try {
                    publishAsset(localPath, payloads[writer]);
                } catch (const std::runtime_error&) {
                    failures.fetch_add(1);
                }

                const std::string seen = readFile(localPath);

                if (seen.size() != kPayloadBytes || seen.find_first_not_of(seen.front()) != std::string::npos) {
                    failures.fetch_add(1);
                }
            }
        });
    }

    for (std::thread& writer : writers) {
        writer.join();
    }

    EXPECT_EQ(failures.load(), 0U);
    EXPECT_EQ(directoryEntries(directory_), std::vector<std::string>{"ExponentAsset-shared.bin"});
}

/** A publish that cannot rename leaves no temporary file behind. */
TEST_F(ExpoAssetTest, RemovesItsTemporaryFileWhenThePublishFails) {
    const std::filesystem::path occupied = directory_ / "ExponentAsset-occupied.bin";

    std::filesystem::create_directories(occupied / "child");

    EXPECT_THROW(publishAsset(occupied, "bytes"), std::runtime_error);
    EXPECT_EQ(directoryEntries(directory_), std::vector<std::string>{"ExponentAsset-occupied.bin"});
}

} // namespace
} // namespace react_native_linux
