#include "BeastWebSocketClient.h"

#include <boost/asio/connect.hpp>
#include <boost/asio/post.hpp>
#include <boost/beast/core/buffers_to_string.hpp>
#include <boost/beast/websocket.hpp>
#include <cstddef>
#include <folly/Uri.h>
#include <memory>
#include <string>
#include <utility>

namespace react_native_linux {

namespace {

constexpr char kWebSocketScheme[] = "ws";
constexpr char kDefaultPort[] = "80";

} // namespace

std::optional<WebSocketEndpoint> parseWebSocketUrl(const std::string& url) {
    const auto uri = folly::Uri::tryFromString(url);

    if (!uri.hasValue() || uri->scheme() != kWebSocketScheme) {
        return std::nullopt;
    }

    const bool namesPort = uri->port() != 0;
    const std::string port = namesPort ? std::to_string(uri->port()) : kDefaultPort;

    return WebSocketEndpoint{
        .hostname = uri->hostname(),
        .port = port,
        .hostHeader = namesPort ? uri->host() + ":" + port : uri->host(),
        .resource = (uri->path().empty() ? "/" : uri->path()) + (uri->query().empty() ? "" : "?" + uri->query()),
    };
}

BeastWebSocketClient::BeastWebSocketClient(std::chrono::milliseconds handshakeTimeout)
    : handshakeTimeout_(handshakeTimeout), onConnect_([](bool, const std::string&) {}),
      onClosed_([](const std::string&) {}), onMessage_([](const std::string&) {}) {}

BeastWebSocketClient::~BeastWebSocketClient() { close("the WebSocket client was destroyed"); }

void BeastWebSocketClient::setOnClosedCallback(OnClosedCallback&& callback) noexcept {
    onClosed_ = std::move(callback);
}

void BeastWebSocketClient::setOnMessageCallback(OnMessageCallback&& callback) noexcept {
    onMessage_ = std::move(callback);
}

void BeastWebSocketClient::connect(const std::string& url, OnConnectCallback&& onConnect) {
    if (onConnect) {
        onConnect_ = std::move(onConnect);
    }

    std::optional<WebSocketEndpoint> endpoint = parseWebSocketUrl(url);

    if (endpoint.has_value()) {
        endpoint_ = std::move(endpoint.value());
        resolver_.async_resolve(
            endpoint_.hostname, endpoint_.port,
            [this](const boost::system::error_code& error,
                   const boost::asio::ip::tcp::resolver::results_type& results) { onResolved(error, results); });
    } else {
        boost::asio::post(ioContext_, [this, url]() { onConnect_(false, "not a ws:// URL: " + url); });
    }

    ioThread_ = std::thread([this]() { ioContext_.run(); });
}

void BeastWebSocketClient::close(const std::string& reason) {
    reportClosed(reason);

    boost::asio::post(ioContext_, [this]() {
        resolver_.cancel();

        if (isOpen_) {
            isOpen_ = false;
            stream_.async_close(boost::beast::websocket::close_code::normal, [](const boost::system::error_code&) {});
        } else {
            boost::beast::get_lowest_layer(stream_).close();
        }
    });

    if (ioThread_.joinable()) {
        ioThread_.join();
    }
}

void BeastWebSocketClient::send(const std::string& message) {
    boost::asio::post(ioContext_, [this, message]() {
        pendingWrites_.push_back(message);

        if (isOpen_) {
            writeNext();
        }
    });
}

void BeastWebSocketClient::ping() {
    boost::asio::post(ioContext_, [this]() { stream_.async_ping({}, [](const boost::system::error_code&) {}); });
}

void BeastWebSocketClient::onResolved(const boost::system::error_code& error,
                                      const boost::asio::ip::tcp::resolver::results_type& results) {
    if (error) {
        onConnect_(false, error.message());
        return;
    }

    boost::beast::get_lowest_layer(stream_).expires_after(handshakeTimeout_);
    boost::beast::get_lowest_layer(stream_).async_connect(
        results, [this](const boost::system::error_code& connectError, const boost::asio::ip::tcp::endpoint&) {
            onConnected(connectError);
        });
}

void BeastWebSocketClient::onConnected(const boost::system::error_code& error) {
    if (error) {
        onConnect_(false, error.message());
        return;
    }

    // Beast's own timeouts take over from the TCP stream's once the socket is connected, and its handshake timeout
    // bounds the closing handshake as well as the opening one.
    boost::beast::get_lowest_layer(stream_).expires_never();
    stream_.set_option(boost::beast::websocket::stream_base::timeout{
        .handshake_timeout = handshakeTimeout_,
        .idle_timeout = boost::beast::websocket::stream_base::none(),
        .keep_alive_pings = false,
    });
    stream_.async_handshake(endpoint_.hostHeader, endpoint_.resource,
                            [this](const boost::system::error_code& handshakeError) { onHandshake(handshakeError); });
}

void BeastWebSocketClient::onHandshake(const boost::system::error_code& error) {
    if (error) {
        onConnect_(false, error.message());
        return;
    }

    isOpen_ = true;
    onConnect_(true, "Connected");
    readNext();
    writeNext();
}

void BeastWebSocketClient::readNext() {
    stream_.async_read(readBuffer_, [this](const boost::system::error_code& error, std::size_t) {
        if (error) {
            isOpen_ = false;
            reportClosed(error.message());
            return;
        }

        onMessage_(boost::beast::buffers_to_string(readBuffer_.data()));
        readBuffer_.consume(readBuffer_.size());
        readNext();
    });
}

// A failed write still dequeues its message: the read that fails beside it reports the close, and the writes
// still queued drain against the closed stream rather than wait for one that is never going to open.
void BeastWebSocketClient::writeNext() {
    if (isWriting_ || pendingWrites_.empty()) {
        return;
    }

    isWriting_ = true;
    stream_.text(true);
    stream_.async_write(boost::asio::buffer(pendingWrites_.front()),
                        [this](const boost::system::error_code&, std::size_t) {
                            pendingWrites_.pop_front();
                            isWriting_ = false;
                            writeNext();
                        });
}

void BeastWebSocketClient::reportClosed(const std::string& reason) {
    if (!hasReportedClose_.exchange(true)) {
        onClosed_(reason);
    }
}

} // namespace react_native_linux
