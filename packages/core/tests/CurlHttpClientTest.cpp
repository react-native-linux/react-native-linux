#include "CurlHttpClient.h"

#include <arpa/inet.h>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <folly/io/IOBuf.h>
#include <fstream>
#include <functional>
#include <future>
#include <gtest/gtest.h>
#include <memory>
#include <mutex>
#include <netinet/in.h>
#include <string>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>
#include <utility>
#include <vector>

namespace react_native_linux {
namespace {

namespace http = facebook::react::http;

constexpr size_t kLargeBodyBytes = 12U * 1024U * 1024U;
constexpr size_t kWriteChunkBytes = 64U * 1024U;
constexpr std::chrono::seconds kCompletionBudget{30};

/**
 * A one-thread HTTP/1.1 server on an ephemeral loopback port. Each connection's request, headers and body, is read
 * whole and handed to the route, which writes the response on the socket itself; a route that writes nothing
 * holds the connection open until the server stops, which is how a timeout and a cancellation are staged.
 */
class LoopbackServer final {
public:
    using Route = std::function<void(const std::string& request, int socket)>;

    explicit LoopbackServer(Route route) : route_(std::move(route)) {
        listener_ = ::socket(AF_INET, SOCK_STREAM, 0);
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        ::bind(listener_, reinterpret_cast<sockaddr*>(&address), sizeof(address));
        ::listen(listener_, 8);
        socklen_t length = sizeof(address);
        ::getsockname(listener_, reinterpret_cast<sockaddr*>(&address), &length);
        port_ = ntohs(address.sin_port);
        thread_ = std::thread([this]() { serve(); });
    }

    LoopbackServer(const LoopbackServer&) = delete;
    LoopbackServer& operator=(const LoopbackServer&) = delete;

    ~LoopbackServer() {
        stopped_.store(true);
        ::shutdown(listener_, SHUT_RDWR);
        ::close(listener_);
        thread_.join();

        for (const int connection : held_) {
            ::close(connection);
        }
    }

    std::string url(const std::string& path) const { return "http://127.0.0.1:" + std::to_string(port_) + path; }

    std::string lastRequest() {
        const std::lock_guard<std::mutex> guard(mutex_);

        return lastRequest_;
    }

private:
    static std::string readRequest(int connection) {
        std::string request;
        std::array<char, 4096> buffer{};

        while (true) {
            const size_t headerEnd = request.find("\r\n\r\n");

            if (headerEnd != std::string::npos) {
                const size_t lengthField = request.find("Content-Length: ");
                const size_t bodyLength =
                    lengthField == std::string::npos ? 0 : std::stoul(request.substr(lengthField + 16));

                if (request.size() >= headerEnd + 4 + bodyLength) {
                    return request;
                }
            }

            const ssize_t count = ::recv(connection, buffer.data(), buffer.size(), 0);

            if (count <= 0) {
                return request;
            }

            request.append(buffer.data(), static_cast<size_t>(count));
        }
    }

    void serve() {
        while (!stopped_.load()) {
            const int connection = ::accept(listener_, nullptr, nullptr);

            if (connection < 0) {
                return;
            }

            const std::string request = readRequest(connection);

            {
                const std::lock_guard<std::mutex> guard(mutex_);
                lastRequest_ = request;
            }

            route_(request, connection);
            held_.push_back(connection);
        }
    }

    Route route_;
    int listener_{-1};
    uint16_t port_{0};
    std::atomic<bool> stopped_{false};
    std::mutex mutex_;
    std::string lastRequest_;
    std::vector<int> held_;
    std::thread thread_;
};

void writeAll(int socket, const std::string& bytes) {
    size_t written = 0;

    while (written < bytes.size()) {
        const ssize_t count = ::send(socket, bytes.data() + written, bytes.size() - written, MSG_NOSIGNAL);

        if (count <= 0) {
            return;
        }

        written += static_cast<size_t>(count);
    }
}

void respond(int socket, const std::string& status, const std::string& headers, const std::string& body) {
    writeAll(socket, "HTTP/1.1 " + status + "\r\nContent-Length: " + std::to_string(body.size()) +
                         "\r\nConnection: close\r\n" + headers + "\r\n" + body);
    ::shutdown(socket, SHUT_WR);
}

void respondWithLargeBody(int socket) {
    writeAll(socket,
             "HTTP/1.1 200 OK\r\nContent-Length: " + std::to_string(kLargeBodyBytes) + "\r\nConnection: close\r\n\r\n");

    for (size_t sent = 0; sent < kLargeBodyBytes; sent += kWriteChunkBytes) {
        writeAll(socket, std::string(kWriteChunkBytes, 'x'));
    }

    ::shutdown(socket, SHUT_WR);
}

/** Every callback of one request, recorded on the client's worker thread and read once it has completed. */
struct Recording {
    std::mutex mutex;
    uint16_t status{0};
    http::Headers headers;
    std::string body;
    int bodyDeliveries{0};
    int64_t incrementalBytes{0};
    int incrementalDeliveries{0};
    int progressReports{0};
    int uploadProgressReports{0};
    std::string error;
    bool timedOut{false};
    std::promise<void> completed;

    http::NetworkCallbacks callbacks(bool incremental = false, bool progress = false) {
        return http::NetworkCallbacks{
            .onUploadProgress = [this](int64_t /*progress*/, int64_t /*total*/) { ++uploadProgressReports; },
            .onResponse =
                [this](uint16_t responseCode, http::Headers responseHeaders) {
                    status = responseCode;
                    headers = std::move(responseHeaders);
                },
            .onBody =
                [this](std::unique_ptr<folly::IOBuf> data) {
                    ++bodyDeliveries;
                    body = data->moveToFbString().toStdString();
                },
            .onBodyIncremental =
                [this](int64_t /*progress*/, int64_t /*total*/, std::unique_ptr<folly::IOBuf> data) {
                    ++incrementalDeliveries;
                    incrementalBytes += static_cast<int64_t>(data->computeChainDataLength());

                    return static_cast<int64_t>(data->computeChainDataLength());
                },
            .onBodyProgress = [this](int64_t /*loaded*/, int64_t /*total*/) { ++progressReports; },
            .onResponseComplete =
                [this](const std::string& completionError, bool timeoutError) {
                    error = completionError;
                    timedOut = timeoutError;
                    completed.set_value();
                },
            .sendIncrementalUpdates = incremental,
            .sendProgressUpdates = progress,
        };
    }

    bool waitForCompletion() { return completed.get_future().wait_for(kCompletionBudget) == std::future_status::ready; }

    std::string header(const std::string& name) const {
        for (const auto& [headerName, value] : headers) {
            if (headerName == name) {
                return value;
            }
        }

        return "";
    }
};

TEST(CurlHttpClientTest, DeliversTheStatusTheHeadersAndTheBufferedBodyOnce) {
    LoopbackServer server([](const std::string& /*request*/, int socket) {
        respond(socket, "200 OK", "X-Probe: yes\r\nX-Empty:\r\n", "hello");
    });
    CurlHttpClient client;
    Recording recording;

    client.sendRequest(recording.callbacks(), "GET", server.url("/hello"), {{"Accept", "text/plain"}}, {}, 0,
                       std::nullopt);

    ASSERT_TRUE(recording.waitForCompletion());
    EXPECT_EQ(recording.status, 200);
    EXPECT_EQ(recording.header("X-Probe"), "yes");
    EXPECT_EQ(recording.header("X-Empty"), "");
    EXPECT_EQ(recording.body, "hello");
    EXPECT_EQ(recording.bodyDeliveries, 1);
    EXPECT_EQ(recording.error, "");
    EXPECT_NE(server.lastRequest().find("Accept: text/plain"), std::string::npos);
}

/**
 * The two halves of the streaming contract over one 12 MB body: incremental delivery never buffers, and buffered
 * delivery has no ceiling.
 */
TEST(CurlHttpClientTest, StreamsATwelveMegabyteDownloadIncrementallyAndBuffersOneWithoutACeiling) {
    LoopbackServer server([](const std::string& /*request*/, int socket) { respondWithLargeBody(socket); });
    CurlHttpClient client;
    Recording streamed;
    Recording buffered;

    client.sendRequest(streamed.callbacks(true), "GET", server.url("/large"), {}, {}, 0, std::nullopt);
    ASSERT_TRUE(streamed.waitForCompletion());
    client.sendRequest(buffered.callbacks(false, true), "GET", server.url("/large"), {}, {}, 0, std::nullopt);
    ASSERT_TRUE(buffered.waitForCompletion());

    EXPECT_EQ(streamed.incrementalBytes, static_cast<int64_t>(kLargeBodyBytes));
    EXPECT_GT(streamed.incrementalDeliveries, 1);
    EXPECT_EQ(streamed.bodyDeliveries, 0);
    EXPECT_EQ(buffered.body.size(), kLargeBodyBytes);
    EXPECT_GT(buffered.progressReports, 1);
}

TEST(CurlHttpClientTest, UploadsMultipartFormDataWithAStringFieldAndAFile) {
    const std::filesystem::path filePath = std::filesystem::temp_directory_path() / "rnl-curl-upload.txt";
    std::ofstream(filePath) << "file-contents";
    LoopbackServer server([](const std::string& /*request*/, int socket) { respond(socket, "201 Created", "", ""); });
    CurlHttpClient client;
    Recording recording;
    const http::FormData formData{
        {.fieldName = "note", .headers = {{"Content-Type", "text/plain"}}, .string = "hi", .uri = std::nullopt},
        {.fieldName = "upload", .headers = {}, .string = std::nullopt, .uri = "file://" + filePath.string()},
        {.fieldName = "bare", .headers = {{"X-Other-Name", "1"}}, .string = std::nullopt, .uri = filePath.string()},
        {.fieldName = "empty", .headers = {}, .string = std::nullopt, .uri = std::nullopt},
    };

    client.sendRequest(recording.callbacks(), "POST", server.url("/upload"), {}, {.formData = formData}, 0,
                       std::nullopt);

    ASSERT_TRUE(recording.waitForCompletion());
    const std::string request = server.lastRequest();
    EXPECT_EQ(recording.status, 201);
    EXPECT_NE(request.find("Content-Type: multipart/form-data"), std::string::npos);
    EXPECT_NE(request.find("name=\"note\""), std::string::npos);
    EXPECT_NE(request.find("Content-Type: text/plain"), std::string::npos);
    EXPECT_NE(request.find("file-contents"), std::string::npos);
    EXPECT_GT(recording.uploadProgressReports, 0);
    EXPECT_EQ(recording.body, "");
    std::filesystem::remove(filePath);
}

TEST(CurlHttpClientTest, SendsAStringBodyAndADecodedBase64Body) {
    LoopbackServer server([](const std::string& /*request*/, int socket) { respond(socket, "200 OK", "", "ok"); });
    CurlHttpClient client;
    Recording stringRecording;
    Recording base64Recording;

    client.sendRequest(stringRecording.callbacks(), "PUT", server.url("/string"), {}, {.string = "plain-body"}, 0,
                       std::nullopt);
    ASSERT_TRUE(stringRecording.waitForCompletion());
    EXPECT_NE(server.lastRequest().find("plain-body"), std::string::npos);

    client.sendRequest(base64Recording.callbacks(), "POST", server.url("/base64"), {}, {.base64 = "YmluYXJ5IQ=="}, 0,
                       std::nullopt);
    ASSERT_TRUE(base64Recording.waitForCompletion());
    EXPECT_NE(server.lastRequest().find("binary!"), std::string::npos);
}

TEST(CurlHttpClientTest, FailsARequestWhoseBodyCannotBeSentWithoutContactingTheServer) {
    CurlHttpClient client;
    Recording blob;
    Recording badBase64;
    Recording missingFile;
    const http::FormData missing{
        {.fieldName = "f", .headers = {}, .string = std::nullopt, .uri = "file:///nonexistent/rnl/file"}};

    client.sendRequest(blob.callbacks(), "POST", "http://127.0.0.1:9/", {}, {.blob = "blob-id"}, 0, std::nullopt);
    client.sendRequest(badBase64.callbacks(), "POST", "http://127.0.0.1:9/", {}, {.base64 = "*"}, 0, std::nullopt);
    client.sendRequest(missingFile.callbacks(), "POST", "http://127.0.0.1:9/", {}, {.formData = missing}, 0,
                       std::nullopt);

    ASSERT_TRUE(blob.waitForCompletion());
    ASSERT_TRUE(badBase64.waitForCompletion());
    ASSERT_TRUE(missingFile.waitForCompletion());
    EXPECT_EQ(blob.error, "a blob request body needs the Blob module, which this platform does not ship yet");
    EXPECT_EQ(badBase64.error, "the base64 request body is not valid base64");
    EXPECT_EQ(missingFile.error, "the form-data file /nonexistent/rnl/file cannot be read");
}

TEST(CurlHttpClientTest, FollowsARedirectAndReportsOnlyTheFinalResponsesHeaders) {
    std::string target;
    LoopbackServer server([&target](const std::string& request, int socket) {
        if (request.starts_with("GET /from")) {
            respond(socket, "302 Found", "Location: " + target + "\r\nX-Hop: first\r\n", "");
        } else {
            respond(socket, "200 OK", "X-Hop: final\r\n", "arrived");
        }
    });
    target = server.url("/to");
    CurlHttpClient client;
    Recording recording;

    client.sendRequest(recording.callbacks(), "GET", server.url("/from"), {}, {}, 0, std::nullopt);

    ASSERT_TRUE(recording.waitForCompletion());
    EXPECT_EQ(recording.status, 200);
    EXPECT_EQ(recording.header("X-Hop"), "final");
    EXPECT_EQ(recording.body, "arrived");
}

TEST(CurlHttpClientTest, AnswersAHeadRequestAndAnEmptyBodyWithAnEmptyBody) {
    LoopbackServer server(
        [](const std::string& /*request*/, int socket) { respond(socket, "204 No Content", "", ""); });
    CurlHttpClient client;
    Recording recording;

    client.sendRequest(recording.callbacks(), "HEAD", server.url("/"), {}, {}, 0, std::nullopt);

    ASSERT_TRUE(recording.waitForCompletion());
    EXPECT_EQ(recording.status, 204);
    EXPECT_EQ(recording.bodyDeliveries, 1);
    EXPECT_EQ(recording.body, "");
}

TEST(CurlHttpClientTest, ReportsATimeoutAsATimeoutAndARefusedConnectionAsAnError) {
    LoopbackServer server([](const std::string& /*request*/, int /*socket*/) {});
    CurlHttpClient client;
    Recording timedOut;
    Recording refused;

    client.sendRequest(timedOut.callbacks(), "GET", server.url("/hang"), {}, {}, 200, std::nullopt);
    client.sendRequest(refused.callbacks(), "GET", "http://127.0.0.1:9/", {}, {}, 0, std::nullopt);

    ASSERT_TRUE(timedOut.waitForCompletion());
    ASSERT_TRUE(refused.waitForCompletion());
    EXPECT_TRUE(timedOut.timedOut);
    EXPECT_FALSE(timedOut.error.empty());
    EXPECT_FALSE(refused.timedOut);
    EXPECT_FALSE(refused.error.empty());
}

TEST(CurlHttpClientTest, ACancelledRequestAndOneInFlightAtTeardownCompleteWithNoCallback) {
    LoopbackServer server([](const std::string& /*request*/, int /*socket*/) {});
    Recording cancelled;
    Recording inFlight;
    std::unique_ptr<http::IRequestToken> token;

    {
        CurlHttpClient client;
        token = client.sendRequest(cancelled.callbacks(), "GET", server.url("/hang"), {}, {}, 0, std::nullopt);
        client.sendRequest(inFlight.callbacks(), "GET", server.url("/hang"), {}, {}, 0, std::nullopt);
        token->cancel();
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
    }

    token->cancel();
    EXPECT_EQ(cancelled.completed.get_future().wait_for(std::chrono::seconds(0)), std::future_status::timeout);
    EXPECT_EQ(inFlight.completed.get_future().wait_for(std::chrono::seconds(0)), std::future_status::timeout);
}

TEST(CurlHttpClientTest, RunsARequestThatSetNoOptionalCallbacks) {
    LoopbackServer server([](const std::string& /*request*/, int socket) { respond(socket, "200 OK", "", "x"); });
    CurlHttpClient client;
    std::promise<std::string> completed;
    std::string body;

    client.sendRequest(
        http::NetworkCallbacks{
            .onBody = [&body](std::unique_ptr<folly::IOBuf> data) { body = data->moveToFbString().toStdString(); },
            .onResponseComplete = [&completed](const std::string& error,
                                               bool /*timeout*/) { completed.set_value(error); },
        },
        "POST", server.url("/"), {}, {.string = "payload"}, 0, std::nullopt);

    auto future = completed.get_future();
    ASSERT_EQ(future.wait_for(kCompletionBudget), std::future_status::ready);
    EXPECT_EQ(future.get(), "");
    EXPECT_EQ(body, "x");
}

TEST(CurlHttpClientTest, DecodesBase64WithAndWithoutPaddingAndRejectsForeignCharacters) {
    EXPECT_EQ(decodeBase64("aGk="), "hi");
    EXPECT_EQ(decodeBase64("aGk"), "hi");
    EXPECT_EQ(decodeBase64("+/8="), std::string("\xfb\xff", 2));
    EXPECT_EQ(decodeBase64(""), "");
    EXPECT_EQ(decodeBase64("a-b"), std::nullopt);
}

} // namespace
} // namespace react_native_linux
