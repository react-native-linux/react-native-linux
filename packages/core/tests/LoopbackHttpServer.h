#pragma once

#include <arpa/inet.h>
#include <array>
#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>
#include <netinet/in.h>
#include <string>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>
#include <utility>
#include <vector>

namespace react_native_linux {

/**
 * For the HTTP client's unit tests and the image pipeline's remote-source tests: a one-thread HTTP/1.1 server on an
 * ephemeral loopback port. Each connection's request, headers and body, is read whole and handed to the route, which
 * writes the response on the socket itself; a route that writes nothing holds the connection open until the server
 * stops, which is how a timeout and a cancellation are staged.
 */
class LoopbackHttpServer final {
public:
    using Route = std::function<void(const std::string& request, int socket)>;

    explicit LoopbackHttpServer(Route route) : route_(std::move(route)) {
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

    LoopbackHttpServer(const LoopbackHttpServer&) = delete;
    LoopbackHttpServer& operator=(const LoopbackHttpServer&) = delete;

    ~LoopbackHttpServer() {
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

inline void writeAll(int socket, const std::string& bytes) {
    size_t written = 0;

    while (written < bytes.size()) {
        const ssize_t count = ::send(socket, bytes.data() + written, bytes.size() - written, MSG_NOSIGNAL);

        if (count <= 0) {
            return;
        }

        written += static_cast<size_t>(count);
    }
}

inline void respond(int socket, const std::string& status, const std::string& headers, const std::string& body) {
    writeAll(socket, "HTTP/1.1 " + status + "\r\nContent-Length: " + std::to_string(body.size()) +
                         "\r\nConnection: close\r\n" + headers + "\r\n" + body);
    ::shutdown(socket, SHUT_WR);
}

} // namespace react_native_linux
