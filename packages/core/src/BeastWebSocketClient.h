#pragma once

#include <atomic>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/beast/core/flat_buffer.hpp>
#include <boost/beast/core/tcp_stream.hpp>
#include <boost/beast/websocket/stream.hpp>
#include <chrono>
#include <deque>
#include <optional>
#include <string>
#include <thread>

#include <react/http/IWebSocketClient.h>

namespace react_native_linux {

/** Where a `ws://` URL points, split the way the resolver and the opening handshake each need it. */
struct WebSocketEndpoint {
    std::string hostname;
    std::string port;
    std::string hostHeader;
    std::string resource;
};

/**
 * `url` as a `ws://` endpoint, with port 80 when it names none (RFC 6455 §3). Empty for anything that is not a
 * `ws://` URL, `wss://` included: this client has no TLS.
 */
std::optional<WebSocketEndpoint> parseWebSocketUrl(const std::string& url);

/**
 * `ReactCxxPlatform`'s `IWebSocketClient` over Boost.Beast, which is what makes upstream's `WebSocketModule` — and
 * so the JavaScript `WebSocket` — work on this platform (#79).
 *
 * It replaces upstream's own cxx `WebSocketClient`, which this platform shipped first and which has three defects
 * a Metro HMR socket hits: a URL without a port resolves port 0, a close handshake the peer never answers blocks
 * the closing thread forever, and a close the server starts is logged rather than reported. Here the port
 * defaults to 80, `handshakeTimeout` bounds connecting and both handshakes, and every way a socket ends reaches
 * the closed callback exactly once.
 *
 * Threading contract: every member may be called from any thread. The socket is only ever touched on the one I/O
 * thread `connect` starts, so the others post their work to it, and every callback runs on it. `close` reports
 * the close before it returns, as upstream's does, because `WebSocket.js` waits for `websocketClosed`; it then
 * joins the I/O thread, which `handshakeTimeout` bounds. The destructor closes, so no callback runs after it.
 */
class BeastWebSocketClient final : public facebook::react::IWebSocketClient {
public:
    static constexpr std::chrono::milliseconds kDefaultHandshakeTimeout{5000};

    explicit BeastWebSocketClient(std::chrono::milliseconds handshakeTimeout = kDefaultHandshakeTimeout);
    BeastWebSocketClient(const BeastWebSocketClient&) = delete;
    BeastWebSocketClient(BeastWebSocketClient&&) = delete;
    BeastWebSocketClient& operator=(const BeastWebSocketClient&) = delete;
    BeastWebSocketClient& operator=(BeastWebSocketClient&&) = delete;
    ~BeastWebSocketClient() override;

    void setOnClosedCallback(OnClosedCallback&& callback) noexcept override;
    void setOnMessageCallback(OnMessageCallback&& callback) noexcept override;
    void connect(const std::string& url, OnConnectCallback&& onConnect = nullptr) override;
    void close(const std::string& reason) override;
    void send(const std::string& message) override;
    void ping() override;

private:
    void onResolved(const boost::system::error_code& error,
                    const boost::asio::ip::tcp::resolver::results_type& results);
    void onConnected(const boost::system::error_code& error);
    void onHandshake(const boost::system::error_code& error);
    void readNext();
    void writeNext();
    void reportClosed(const std::string& reason);

    std::chrono::milliseconds handshakeTimeout_;
    OnConnectCallback onConnect_;
    OnClosedCallback onClosed_;
    OnMessageCallback onMessage_;
    WebSocketEndpoint endpoint_;
    boost::asio::io_context ioContext_;
    boost::asio::ip::tcp::resolver resolver_{ioContext_};
    boost::beast::websocket::stream<boost::beast::tcp_stream> stream_{ioContext_};
    boost::beast::flat_buffer readBuffer_;
    std::deque<std::string> pendingWrites_;
    bool isOpen_{false};
    bool isWriting_{false};
    std::atomic<bool> hasReportedClose_{false};
    std::thread ioThread_;
};

} // namespace react_native_linux
