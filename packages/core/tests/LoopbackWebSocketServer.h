#pragma once

#include <array>
#include <boost/asio/buffer.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/beast/core/buffers_to_string.hpp>
#include <boost/beast/core/flat_buffer.hpp>
#include <boost/beast/websocket.hpp>
#include <future>
#include <string>
#include <string_view>
#include <thread>
#include <utility>

namespace react_native_linux {

/** How a `LoopbackWebSocketServer` session ended. */
struct LoopbackSessionEnd {
    bool closedByCloseFrame;
    int pingCount;
};

/**
 * One WebSocket session's worth of server on loopback, for the WebSocket client's unit tests and the WebSocket
 * module's Hermes tests: it accepts a single client and echoes every text message back. Two messages are requests
 * rather than text, so a test can drive the endings a client has to survive:
 *
 * - `kServerCloseRequest` is echoed, then the server starts the close handshake itself.
 * - `kStallRequest` makes the server stop speaking WebSocket and drain raw bytes until the client drops the socket,
 *   so a close frame from the client is read and never answered.
 *
 * Threading contract: the session runs on a thread this object owns and joins on destruction, so a test must
 * connect a client to every server it constructs.
 */
class LoopbackWebSocketServer final {
public:
    static constexpr std::string_view kServerCloseRequest = "close";
    static constexpr std::string_view kStallRequest = "stall";

    LoopbackWebSocketServer()
        : acceptor_(ioContext_, boost::asio::ip::tcp::endpoint(boost::asio::ip::make_address("127.0.0.1"), 0)),
          thread_([this]() { serve(); }) {}

    LoopbackWebSocketServer(const LoopbackWebSocketServer&) = delete;
    LoopbackWebSocketServer(LoopbackWebSocketServer&&) = delete;
    LoopbackWebSocketServer& operator=(const LoopbackWebSocketServer&) = delete;
    LoopbackWebSocketServer& operator=(LoopbackWebSocketServer&&) = delete;

    ~LoopbackWebSocketServer() { thread_.join(); }

    std::string url() const { return "ws://127.0.0.1:" + std::to_string(acceptor_.local_endpoint().port()); }

    /** Blocks until the session has ended. */
    LoopbackSessionEnd sessionEnd() { return ended_.get_future().get(); }

private:
    void serve() {
        namespace websocket = boost::beast::websocket;

        boost::system::error_code error;
        boost::asio::ip::tcp::socket socket{ioContext_};
        int pingCount = 0;

        acceptor_.accept(socket, error);

        websocket::stream<boost::asio::ip::tcp::socket> stream{std::move(socket)};

        stream.control_callback([&pingCount](websocket::frame_type kind, boost::beast::string_view) {
            pingCount += kind == websocket::frame_type::ping ? 1 : 0;
        });

        if (!error) {
            stream.accept(error);
        }

        boost::beast::flat_buffer buffer;

        while (!error) {
            stream.read(buffer, error);

            if (error) {
                break;
            }

            const std::string message = boost::beast::buffers_to_string(buffer.data());

            buffer.consume(buffer.size());

            if (message == kStallRequest) {
                std::array<char, 256> bytes{};

                while (!error) {
                    stream.next_layer().read_some(boost::asio::buffer(bytes), error);
                }

                ended_.set_value({.closedByCloseFrame = false, .pingCount = pingCount});
                return;
            }

            stream.text(true);
            stream.write(boost::asio::buffer(message), error);

            if (!error && message == kServerCloseRequest) {
                stream.close(websocket::close_code::normal, error);
                ended_.set_value({.closedByCloseFrame = !error, .pingCount = pingCount});
                return;
            }
        }

        ended_.set_value({.closedByCloseFrame = error == websocket::error::closed, .pingCount = pingCount});
    }

    boost::asio::io_context ioContext_;
    boost::asio::ip::tcp::acceptor acceptor_;
    std::promise<LoopbackSessionEnd> ended_;
    std::thread thread_;
};

/** A loopback port nothing listens on: bound and released, so a connection to it is refused. */
inline std::string refusedWebSocketUrl() {
    boost::asio::io_context ioContext;
    const boost::asio::ip::tcp::acceptor acceptor{
        ioContext, boost::asio::ip::tcp::endpoint(boost::asio::ip::make_address("127.0.0.1"), 0)};

    return "ws://127.0.0.1:" + std::to_string(acceptor.local_endpoint().port());
}

} // namespace react_native_linux
