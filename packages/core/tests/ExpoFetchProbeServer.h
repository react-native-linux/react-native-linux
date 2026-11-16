#pragma once

#include "LoopbackHttpServer.h"

#include <array>
#include <chrono>
#include <functional>
#include <future>
#include <poll.h>
#include <string>
#include <sys/socket.h>
#include <utility>

namespace react_native_linux {

/**
 * The `expo/fetch` tests' loopback routes: `/text` answers `hello`, `/redirect` redirects there, `/echo` answers the
 * request it read, `/trickle` sends `first`, then `second` only once `opened` is ready or ten seconds have passed,
 * `/headers` sends an interim 103 response and the final response's headers, `/unlocated` a 302's headers without a
 * Location, and `/folded` headers with a folded line, each then its body only once `opened` is ready, `/truncated`
 * closes after three of the ten bytes it announced, `/held` sends `first` and then calls `peerClosed` once the client
 * closes the connection, if it does within ten seconds, and every other path never answers.
 */
inline LoopbackHttpServer::Route expoFetchProbeRoutes(std::shared_future<void> opened,
                                                      std::function<void()> peerClosed) {
    return [opened = std::move(opened), peerClosed = std::move(peerClosed)](const std::string& request, int socket) {
        constexpr std::chrono::seconds kGateBudget{10};
        constexpr int kCloseBudgetMilliseconds = 10'000;

        if (request.starts_with("GET /text ")) {
            respond(socket, "200 OK", "X-Probe: yes\r\n", "hello");
        } else if (request.starts_with("GET /redirect ")) {
            respond(socket, "302 Found", "Location: /text\r\n", "moved");
        } else if (request.find(" /echo ") != std::string::npos) {
            respond(socket, "200 OK", "", request);
        } else if (request.starts_with("GET /trickle ")) {
            writeAll(socket, "HTTP/1.1 200 OK\r\nContent-Length: 11\r\nConnection: close\r\n\r\nfirst");
            opened.wait_for(kGateBudget);
            writeAll(socket, "second");
            ::shutdown(socket, SHUT_WR);
        } else if (request.starts_with("GET /headers ")) {
            writeAll(socket, "HTTP/1.1 103 Early Hints\r\nLink: </hint>\r\n\r\n"
                             "HTTP/1.1 200 OK\r\nContent-Length: 4\r\nConnection: close\r\nX-Final: yes\r\n\r\n");
            opened.wait();
            writeAll(socket, "body");
            ::shutdown(socket, SHUT_WR);
        } else if (request.starts_with("GET /unlocated ")) {
            writeAll(socket, "HTTP/1.1 302 Found\r\nContent-Length: 4\r\nConnection: close\r\n\r\n");
            opened.wait();
            writeAll(socket, "body");
            ::shutdown(socket, SHUT_WR);
        } else if (request.starts_with("GET /folded ")) {
            writeAll(socket, "HTTP/1.1 200 OK\r\nX-Note: first\r\n continuation\r\nContent-Type: text/plain\r\n"
                             "Content-Length: 4\r\nConnection: close\r\n\r\n");
            opened.wait();
            writeAll(socket, "body");
            ::shutdown(socket, SHUT_WR);
        } else if (request.starts_with("GET /truncated ")) {
            writeAll(socket, "HTTP/1.1 200 OK\r\nContent-Length: 10\r\nConnection: close\r\n\r\nabc");
            ::shutdown(socket, SHUT_WR);
        } else if (request.starts_with("GET /held ")) {
            writeAll(socket, "HTTP/1.1 200 OK\r\nConnection: close\r\n\r\nfirst");
            pollfd connection{.fd = socket, .events = POLLIN, .revents = 0};
            std::array<char, 1> byte{};

            if (::poll(&connection, 1, kCloseBudgetMilliseconds) == 1 &&
                ::recv(socket, byte.data(), byte.size(), 0) <= 0) {
                peerClosed();
            }
        }
    };
}

} // namespace react_native_linux
