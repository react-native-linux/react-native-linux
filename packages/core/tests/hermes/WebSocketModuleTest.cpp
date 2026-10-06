#include "ReactHost.h"

#include <boost/asio/ip/tcp.hpp>
#include <boost/beast/core/flat_buffer.hpp>
#include <boost/beast/websocket.hpp>
#include <chrono>
#include <cstdint>
#include <cxxreact/JSBigString.h>
#include <future>
#include <gtest/gtest.h>
#include <jsi/jsi.h>
#include <memory>
#include <string>
#include <thread>

// #79's WebSocket slice: the JS `WebSocket` reaches upstream's C++ WebSocket TurboModule, which runs over upstream's
// Boost.Beast client, against an echo server on loopback in this process. A bare script stands in for
// `WebSocket.js` the way networking.js stands in for XMLHttpRequest: it calls the module and listens on the device
// event emitter, which is all `WebSocket.js` does underneath.

namespace react_native_linux {
namespace {

namespace websocket = boost::beast::websocket;
using boost::asio::ip::tcp;

constexpr std::chrono::milliseconds kQuiescenceBudget{5000};

/**
 * One connection's worth of echo server: accepts a single client, returns every text message it receives, and
 * finishes when the client closes. `finished` resolves to whether the close was a WebSocket close frame, which is
 * how a test can tell an orderly client close from a dropped socket.
 */
class LoopbackEchoServer final {
public:
    LoopbackEchoServer()
        : acceptor_(ioContext_, tcp::endpoint(boost::asio::ip::make_address("127.0.0.1"), 0)),
          thread_([this]() { serve(); }) {}

    LoopbackEchoServer(const LoopbackEchoServer&) = delete;
    LoopbackEchoServer(LoopbackEchoServer&&) = delete;
    LoopbackEchoServer& operator=(const LoopbackEchoServer&) = delete;
    LoopbackEchoServer& operator=(LoopbackEchoServer&&) = delete;

    ~LoopbackEchoServer() { thread_.join(); }

    std::string url() const { return "ws://127.0.0.1:" + std::to_string(acceptor_.local_endpoint().port()); }

    bool closedWithCloseFrame() { return finished_.get_future().get(); }

private:
    void serve() {
        tcp::socket socket{ioContext_};
        acceptor_.accept(socket);

        websocket::stream<tcp::socket> stream{std::move(socket)};
        stream.accept();

        boost::beast::flat_buffer buffer;
        boost::system::error_code error;

        for (stream.read(buffer, error); !error; stream.read(buffer, error)) {
            stream.text(true);
            stream.write(buffer.data(), error);
            buffer.consume(buffer.size());
        }

        finished_.set_value(error == websocket::error::closed);
    }

    boost::asio::io_context ioContext_;
    tcp::acceptor acceptor_;
    std::promise<bool> finished_;
    std::thread thread_;
};

/** A TCP port nothing listens on: bound and released, so a connect to it is refused. */
std::string refusedUrl() {
    boost::asio::io_context ioContext;
    const tcp::acceptor acceptor{ioContext, tcp::endpoint(boost::asio::ip::make_address("127.0.0.1"), 0)};

    return "ws://127.0.0.1:" + std::to_string(acceptor.local_endpoint().port());
}

// `afterOpen` decides what the open socket does next; every event is appended to `events`, and the interval only
// keeps the host from reaching quiescence while the socket is on the network, because a socket is not a timer.
std::string scriptConnectingTo(const std::string& url, const std::string& afterOpen) {
    return R"JAVASCRIPT(
const turboModuleProxy = globalThis.__turboModuleProxy;
const webSocket =
  typeof turboModuleProxy === 'function'
    ? turboModuleProxy('WebSocketModule')
    : globalThis.nativeModuleProxy.WebSocketModule;
const socketId = 7;
const keepAlive = setInterval(() => {}, 10);
const settle = () => clearInterval(keepAlive);

globalThis.events = [];
globalThis.__rctDeviceEventEmitter = {
  emit: (eventName, payload) => {
    if (eventName === 'websocketOpen') {
      events.push('open');
      )JAVASCRIPT" +
           afterOpen + R"JAVASCRIPT(
    } else if (eventName === 'websocketMessage') {
      events.push(`message ${payload.data}`);
      webSocket.close(1000, 'done', socketId);
    } else if (eventName === 'websocketClosed') {
      events.push('closed');
      settle();
    } else if (eventName === 'websocketFailed') {
      events.push('failed');
      settle();
    }
  },
};

webSocket.connect(')JAVASCRIPT" +
           url + R"JAVASCRIPT(', null, {}, socketId);
)JAVASCRIPT";
}

std::string recordedEvents(ReactHost& reactHost) {
    std::promise<std::string> events;

    reactHost.reactInstance().getBufferedRuntimeExecutor()([&events](facebook::jsi::Runtime& runtime) {
        const facebook::jsi::Object array = runtime.global().getPropertyAsObject(runtime, "events");
        const facebook::jsi::Function join = array.getPropertyAsFunction(runtime, "join");

        events.set_value(join.callWithThis(runtime, array, ",").getString(runtime).utf8(runtime));
    });

    return events.get_future().get();
}

std::string runScript(ReactHost& reactHost, const std::string& script) {
    reactHost.loadScript(std::make_unique<facebook::react::JSBigStdString>(script), "WebSocketModuleTest.js");

    EXPECT_TRUE(reactHost.runUntilQuiescent(kQuiescenceBudget)) << "the socket never settled";

    return recordedEvents(reactHost);
}

TEST(WebSocketModuleTest, AMessageSentOnOpenIsEchoedBackAndTheClientCloseReachesTheServer) {
    LoopbackEchoServer server;

    {
        ReactHost reactHost;

        EXPECT_EQ(runScript(reactHost, scriptConnectingTo(server.url(), "webSocket.send('hello', socketId);")),
                  "open,message hello,closed");
    }

    EXPECT_TRUE(server.closedWithCloseFrame());
}

TEST(WebSocketModuleTest, ARefusedConnectionFails) {
    ReactHost reactHost;

    EXPECT_EQ(runScript(reactHost, scriptConnectingTo(refusedUrl(), "")), "failed");
}

TEST(WebSocketModuleTest, DestroyingTheHostWithASocketOpenClosesIt) {
    LoopbackEchoServer server;

    {
        ReactHost reactHost;

        EXPECT_EQ(runScript(reactHost, scriptConnectingTo(server.url(), "settle();")), "open");
    }

    EXPECT_TRUE(server.closedWithCloseFrame());
}

} // namespace
} // namespace react_native_linux
