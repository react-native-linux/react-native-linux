#include "ReactHost.h"

#include <boost/asio/ip/tcp.hpp>
#include <boost/beast/core/flat_buffer.hpp>
#include <boost/beast/http.hpp>
#include <chrono>
#include <future>
#include <gtest/gtest.h>
#include <jsi/jsi.h>
#include <string>
#include <thread>
#include <utility>

// #79: a `dev=true` bundle loaded from a dev server gets `HMRClient.setup`, which is what connects it to Metro's
// `/hot` socket — the call upstream's cxx host makes through `DevServerHelper::setupHMRClient`. The bundle served
// here registers a stand-in `HMRClient` that records what it was called with.

namespace react_native_linux {
namespace {

namespace http = boost::beast::http;
using boost::asio::ip::tcp;

constexpr std::chrono::milliseconds kQuiescenceBudget{5000};

constexpr char kRecordingBundle[] = R"JAVASCRIPT(
globalThis.hmrSetup = 'never called';
globalThis.RN$registerCallableModule('HMRClient', () => ({
  setup: (...parameters) => { globalThis.hmrSetup = JSON.stringify(parameters); },
}));
)JAVASCRIPT";

/** Answers exactly one HTTP request on loopback with `kRecordingBundle`. */
class LoopbackBundleServer final {
public:
    LoopbackBundleServer()
        : acceptor_(ioContext_, tcp::endpoint(boost::asio::ip::make_address("127.0.0.1"), 0)),
          thread_([this]() { serve(); }) {}

    LoopbackBundleServer(const LoopbackBundleServer&) = delete;
    LoopbackBundleServer(LoopbackBundleServer&&) = delete;
    LoopbackBundleServer& operator=(const LoopbackBundleServer&) = delete;
    LoopbackBundleServer& operator=(LoopbackBundleServer&&) = delete;

    ~LoopbackBundleServer() { thread_.join(); }

    unsigned short port() const { return acceptor_.local_endpoint().port(); }

private:
    void serve() {
        tcp::socket socket{ioContext_};
        boost::beast::flat_buffer buffer;
        http::request<http::string_body> request;
        http::response<http::string_body> response{http::status::ok, request.version()};

        acceptor_.accept(socket);
        http::read(socket, buffer, request);
        response.set(http::field::content_type, "application/javascript");
        response.body() = kRecordingBundle;
        response.prepare_payload();
        http::write(socket, response);
    }

    boost::asio::io_context ioContext_;
    tcp::acceptor acceptor_;
    std::thread thread_;
};

std::string recordedSetup(ReactHost& reactHost) {
    std::promise<std::string> setup;

    reactHost.reactInstance().getBufferedRuntimeExecutor()([&setup](facebook::jsi::Runtime& runtime) {
        setup.set_value(runtime.global().getProperty(runtime, "hmrSetup").getString(runtime).utf8(runtime));
    });

    return setup.get_future().get();
}

std::string setupAfterLoading(const LoopbackBundleServer& server, const std::string& query) {
    ReactHost reactHost;

    reactHost.loadBundle("http://127.0.0.1:" + std::to_string(server.port()) + "/index.bundle?" + query);
    reactHost.runUntilQuiescent(kQuiescenceBudget);

    return recordedSetup(reactHost);
}

TEST(HotModuleReplacementSetupTest, ADevBundleFromADevServerIsConnectedToItsHotSocket) {
    const LoopbackBundleServer server;

    EXPECT_EQ(setupAfterLoading(server, "platform=linux&dev=true&minify=false"),
              R"(["linux","index.bundle","127.0.0.1",)" + std::to_string(server.port()) + R"(,true,"http"])");
}

TEST(HotModuleReplacementSetupTest, AProductionBundleIsNot) {
    const LoopbackBundleServer server;

    EXPECT_EQ(setupAfterLoading(server, "platform=linux&dev=false&minify=true"), "never called");
}

} // namespace
} // namespace react_native_linux
