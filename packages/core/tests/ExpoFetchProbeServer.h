#pragma once

#include "LoopbackHttpServer.h"

#include <chrono>
#include <future>
#include <string>
#include <sys/socket.h>
#include <utility>

namespace react_native_linux {

/**
 * The `expo/fetch` tests' loopback routes: `/text` answers `hello`, `/redirect` redirects there, `/echo` answers the
 * request it read, `/trickle` sends `first`, then `second` only once `opened` is ready or ten seconds have passed,
 * and every other path never answers.
 */
inline LoopbackHttpServer::Route expoFetchProbeRoutes(std::shared_future<void> opened) {
    return [opened = std::move(opened)](const std::string& request, int socket) {
        constexpr std::chrono::seconds kGateBudget{10};

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
        }
    };
}

} // namespace react_native_linux
