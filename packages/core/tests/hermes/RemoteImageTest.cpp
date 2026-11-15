#include "../LoopbackHttpServer.h"
#include "ImageDecoder.h"
#include "ImagePipeline.h"
#include "include/core/SkImage.h"

#include <chrono>
#include <fstream>
#include <future>
#include <gtest/gtest.h>
#include <iterator>
#include <memory>
#include <optional>
#include <string>

namespace react_native_linux {
namespace {

constexpr std::chrono::seconds kDecodeBudget{30};
constexpr int kTestImageWidth = 64;
constexpr int kTestImageHeight = 48;

using Decoded = std::shared_ptr<const DecodedImageFrames>;

std::string testImageBytes() {
    std::ifstream file(RNL_TEST_IMAGE, std::ios::binary);

    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

/** Requests `uri` and hands back a future of what its completion receives. */
std::future<Decoded> requestDecode(const std::string& uri) {
    auto completed = std::make_shared<std::promise<Decoded>>();
    std::future<Decoded> decoded = completed->get_future();

    requestImageDecode(uri, [completed](const Decoded& frames) { completed->set_value(frames); });

    return decoded;
}

std::optional<Decoded> awaitDecode(std::future<Decoded>& decoded) {
    if (decoded.wait_for(kDecodeBudget) != std::future_status::ready) {
        return std::nullopt;
    }

    return decoded.get();
}

TEST(RemoteImageTest, AnHttpSourceDecodesFromTheBytesTheServerSent) {
    LoopbackHttpServer server(
        [](const std::string& /*request*/, int socket) { respond(socket, "200 OK", "", testImageBytes()); });
    std::future<Decoded> request = requestDecode(server.url("/tile.png"));
    const std::optional<Decoded> decoded = awaitDecode(request);

    ASSERT_TRUE(decoded.has_value());
    ASSERT_NE(decoded.value(), nullptr);

    const auto* frame = static_cast<const SkImage*>(decoded.value()->frames.front().get());

    EXPECT_EQ(frame->width(), kTestImageWidth);
    EXPECT_EQ(frame->height(), kTestImageHeight);
    EXPECT_EQ(decodedImage(server.url("/tile.png")), decoded.value());
}

TEST(RemoteImageTest, AStatusOtherThan2xxFailsEvenWithAnImageForABody) {
    LoopbackHttpServer server(
        [](const std::string& /*request*/, int socket) { respond(socket, "404 Not Found", "", testImageBytes()); });
    std::future<Decoded> request = requestDecode(server.url("/missing.png"));
    const std::optional<Decoded> decoded = awaitDecode(request);

    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(decoded.value(), nullptr);
}

TEST(RemoteImageTest, AStalledDownloadHoldsUpNoLocalDecodeAndFailsWhenTheServerHangsUp) {
    std::future<Decoded> stalled;

    {
        LoopbackHttpServer server([](const std::string& /*request*/, int /*socket*/) {});

        stalled = requestDecode(server.url("/stalled.png"));

        std::future<Decoded> local = requestDecode(std::string("file://") + RNL_TEST_IMAGE);
        const std::optional<Decoded> localDecoded = awaitDecode(local);

        ASSERT_TRUE(localDecoded.has_value());
        EXPECT_NE(localDecoded.value(), nullptr);
        EXPECT_EQ(stalled.wait_for(std::chrono::seconds{0}), std::future_status::timeout);
    }

    const std::optional<Decoded> stalledDecoded = awaitDecode(stalled);

    ASSERT_TRUE(stalledDecoded.has_value());
    EXPECT_EQ(stalledDecoded.value(), nullptr);
    EXPECT_TRUE(waitForPendingImageDecodes(kDecodeBudget));
}

} // namespace
} // namespace react_native_linux
