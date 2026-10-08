#include "../LoopbackWebSocketServer.h"
#include "ReactHost.h"

#include <chrono>
#include <cxxreact/JSBigString.h>
#include <future>
#include <gtest/gtest.h>
#include <jsi/jsi.h>
#include <memory>
#include <string>

// #79's WebSocket slice: the JS `WebSocket` reaches upstream's C++ WebSocket TurboModule, which runs over this
// platform's BeastWebSocketClient, against an echo server on loopback in this process. A bare script stands in for
// `WebSocket.js` the way networking.js stands in for XMLHttpRequest: it calls the module and listens on the device
// event emitter, which is all `WebSocket.js` does underneath.

namespace react_native_linux {
namespace {

constexpr std::chrono::milliseconds kQuiescenceBudget{5000};

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
    LoopbackWebSocketServer server;

    {
        ReactHost reactHost;

        EXPECT_EQ(runScript(reactHost, scriptConnectingTo(server.url(), "webSocket.send('hello', socketId);")),
                  "open,message hello,closed");
    }

    EXPECT_TRUE(server.sessionEnd().closedByCloseFrame);
}

TEST(WebSocketModuleTest, ARefusedConnectionFails) {
    ReactHost reactHost;

    EXPECT_EQ(runScript(reactHost, scriptConnectingTo(refusedWebSocketUrl(), "")), "failed");
}

TEST(WebSocketModuleTest, DestroyingTheHostWithASocketOpenClosesIt) {
    LoopbackWebSocketServer server;

    {
        ReactHost reactHost;

        EXPECT_EQ(runScript(reactHost, scriptConnectingTo(server.url(), "settle();")), "open");
    }

    EXPECT_TRUE(server.sessionEnd().closedByCloseFrame);
}

} // namespace
} // namespace react_native_linux
