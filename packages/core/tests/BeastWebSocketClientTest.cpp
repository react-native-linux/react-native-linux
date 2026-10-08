#include "BeastWebSocketClient.h"

#include "LoopbackWebSocketServer.h"

#include <boost/asio/ip/tcp.hpp>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <gtest/gtest.h>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

// #79: the WebSocket client behind upstream's WebSocketModule, against a server on loopback in this process.

namespace react_native_linux {
namespace {

using namespace std::chrono_literals;

constexpr std::chrono::seconds kEventBudget{10};

/** What a client reported, in order, from the I/O thread its callbacks run on. */
class EventLog final {
public:
    void add(std::string event) {
        {
            const std::scoped_lock lock{mutex_};
            events_.push_back(std::move(event));
        }
        changed_.notify_all();
    }

    std::vector<std::string> waitFor(std::size_t count) {
        std::unique_lock lock{mutex_};

        changed_.wait_for(lock, kEventBudget, [this, count]() { return events_.size() >= count; });

        return events_;
    }

private:
    std::mutex mutex_;
    std::condition_variable changed_;
    std::vector<std::string> events_;
};

void connectRecording(BeastWebSocketClient& client, EventLog& log, const std::string& url) {
    client.setOnMessageCallback([&log](const std::string& message) { log.add("message " + message); });
    client.setOnClosedCallback([&log](const std::string&) { log.add("closed"); });
    client.connect(url, [&log](bool connected, const std::string&) { log.add(connected ? "open" : "failed"); });
}

using Events = std::vector<std::string>;

TEST(BeastWebSocketClientTest, MessagesSentBeforeAndJustAfterTheOpenAreEchoedInOrder) {
    LoopbackWebSocketServer server;
    EventLog log;

    {
        BeastWebSocketClient client;

        client.send("early");
        connectRecording(client, log, server.url());

        ASSERT_EQ(log.waitFor(1).front(), "open");

        client.send("first");
        client.send("second");

        EXPECT_EQ(log.waitFor(4), (Events{"open", "message early", "message first", "message second"}));

        client.close("done");
    }

    EXPECT_EQ(log.waitFor(5), (Events{"open", "message early", "message first", "message second", "closed"}));
    EXPECT_TRUE(server.sessionEnd().closedByCloseFrame);
}

TEST(BeastWebSocketClientTest, ACloseTheServerStartsIsReportedOnce) {
    LoopbackWebSocketServer server;
    EventLog log;

    {
        BeastWebSocketClient client;

        connectRecording(client, log, server.url());
        client.send(std::string{LoopbackWebSocketServer::kServerCloseRequest});

        EXPECT_EQ(log.waitFor(3), (Events{"open", "message close", "closed"}));
        EXPECT_TRUE(server.sessionEnd().closedByCloseFrame);
    }

    EXPECT_EQ(log.waitFor(3).size(), 3U) << "the destructor reported the close a second time";
}

TEST(BeastWebSocketClientTest, ACloseThePeerNeverAnswersReturnsWithinTheHandshakeTimeout) {
    LoopbackWebSocketServer server;
    EventLog log;
    BeastWebSocketClient client{200ms};

    connectRecording(client, log, server.url());

    ASSERT_EQ(log.waitFor(1).front(), "open");

    client.send(std::string{LoopbackWebSocketServer::kStallRequest});

    const auto closeStarted = std::chrono::steady_clock::now();

    client.close("done");

    EXPECT_LT(std::chrono::steady_clock::now() - closeStarted, 5s);
    EXPECT_FALSE(server.sessionEnd().closedByCloseFrame);
    EXPECT_EQ(log.waitFor(2), (Events{"open", "closed"}));
}

TEST(BeastWebSocketClientTest, PingReachesThePeer) {
    LoopbackWebSocketServer server;
    EventLog log;

    {
        BeastWebSocketClient client;

        connectRecording(client, log, server.url());

        ASSERT_EQ(log.waitFor(1).front(), "open");

        client.ping();
        client.send("after the ping");

        EXPECT_EQ(log.waitFor(2).back(), "message after the ping");
    }

    EXPECT_EQ(server.sessionEnd().pingCount, 1);
}

TEST(BeastWebSocketClientTest, DestroyingAnOpenClientClosesIt) {
    LoopbackWebSocketServer server;
    EventLog log;

    {
        BeastWebSocketClient client;

        connectRecording(client, log, server.url());

        ASSERT_EQ(log.waitFor(1).front(), "open");
    }

    EXPECT_TRUE(server.sessionEnd().closedByCloseFrame);
    EXPECT_EQ(log.waitFor(2), (Events{"open", "closed"}));
}

TEST(BeastWebSocketClientTest, AClientWithNoCallbacksRunsASessionToTheEnd) {
    LoopbackWebSocketServer server;
    BeastWebSocketClient client;

    client.connect(server.url(), nullptr);
    client.send(std::string{LoopbackWebSocketServer::kServerCloseRequest});

    EXPECT_TRUE(server.sessionEnd().closedByCloseFrame);
}

TEST(BeastWebSocketClientTest, ARefusedConnectionFails) {
    EventLog log;
    BeastWebSocketClient client;

    connectRecording(client, log, refusedWebSocketUrl());

    EXPECT_EQ(log.waitFor(1), (Events{"failed"}));
}

TEST(BeastWebSocketClientTest, AnUnresolvableHostFails) {
    EventLog log;
    BeastWebSocketClient client;

    // RFC 6761 reserves `.invalid`: no resolver may answer for it.
    connectRecording(client, log, "ws://rnl-websocket-client-test.invalid/");

    EXPECT_EQ(log.waitFor(1), (Events{"failed"}));
}

TEST(BeastWebSocketClientTest, APeerThatDoesNotSpeakWebSocketFailsTheHandshake) {
    boost::asio::io_context ioContext;
    boost::asio::ip::tcp::acceptor acceptor{
        ioContext, boost::asio::ip::tcp::endpoint(boost::asio::ip::make_address("127.0.0.1"), 0)};
    std::thread peer([&acceptor, &ioContext]() {
        boost::asio::ip::tcp::socket socket{ioContext};

        acceptor.accept(socket);
    });
    EventLog log;
    BeastWebSocketClient client{200ms};

    connectRecording(client, log, "ws://127.0.0.1:" + std::to_string(acceptor.local_endpoint().port()));

    EXPECT_EQ(log.waitFor(1), (Events{"failed"}));

    peer.join();
}

TEST(BeastWebSocketClientTest, AUrlThatIsNotWsFailsWithoutConnecting) {
    EventLog log;
    BeastWebSocketClient client;

    connectRecording(client, log, "wss://127.0.0.1:1/");

    EXPECT_EQ(log.waitFor(1), (Events{"failed"}));
}

TEST(BeastWebSocketClientTest, ClosingAClientThatNeverConnectedReportsTheCloseOnce) {
    EventLog log;
    BeastWebSocketClient client;

    client.setOnClosedCallback([&log](const std::string& reason) { log.add("closed " + reason); });
    client.close("never opened");
    client.close("again");

    EXPECT_EQ(log.waitFor(1), (Events{"closed never opened"}));
}

TEST(BeastWebSocketClientTest, AUrlWithoutAPortResolvesPortEighty) {
    const std::optional<WebSocketEndpoint> endpoint = parseWebSocketUrl("ws://localhost");

    ASSERT_TRUE(endpoint.has_value());
    EXPECT_EQ(endpoint->hostname, "localhost");
    EXPECT_EQ(endpoint->port, "80");
    EXPECT_EQ(endpoint->hostHeader, "localhost");
    EXPECT_EQ(endpoint->resource, "/");
}

TEST(BeastWebSocketClientTest, AnExplicitPortPathAndQueryAreKept) {
    const std::optional<WebSocketEndpoint> endpoint = parseWebSocketUrl("ws://[::1]:8081/hot?bundleEntry=index");

    ASSERT_TRUE(endpoint.has_value());
    EXPECT_EQ(endpoint->hostname, "::1");
    EXPECT_EQ(endpoint->port, "8081");
    EXPECT_EQ(endpoint->hostHeader, "[::1]:8081");
    EXPECT_EQ(endpoint->resource, "/hot?bundleEntry=index");
}

TEST(BeastWebSocketClientTest, AnHttpUrlIsTheSameEndpointAsItsWsUrl) {
    const std::optional<WebSocketEndpoint> endpoint = parseWebSocketUrl("http://127.0.0.1:8081/hot");

    ASSERT_TRUE(endpoint.has_value());
    EXPECT_EQ(endpoint->port, "8081");
    EXPECT_EQ(endpoint->resource, "/hot");
}

TEST(BeastWebSocketClientTest, OnlyWsAndHttpUrlsAreEndpoints) {
    EXPECT_FALSE(parseWebSocketUrl("wss://localhost/").has_value());
    EXPECT_FALSE(parseWebSocketUrl("https://localhost/").has_value());
    EXPECT_FALSE(parseWebSocketUrl("not a url").has_value());
}

} // namespace
} // namespace react_native_linux
