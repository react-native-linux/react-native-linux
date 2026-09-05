#include "AutomationServer.h"

#include <array>
#include <cerrno>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <optional>
#include <string>
#include <sys/socket.h>
#include <sys/un.h>
#include <system_error>
#include <unistd.h>
#include <utility>

namespace react_native_linux {

namespace {

constexpr int kListenBacklog = 1;
constexpr size_t kReadChunkBytes = 4096;
constexpr char kLineTerminator = '\n';

[[noreturn]] void throwErrno(const char* syscallName) {
    throw std::system_error(errno, std::generic_category(), syscallName);
}

sockaddr_un makeSocketAddress(const std::string& socketPath) {
    sockaddr_un address{};

    address.sun_family = AF_UNIX;

    if (socketPath.size() >= sizeof(address.sun_path)) {
        throw std::system_error(ENAMETOOLONG, std::generic_category(), socketPath);
    }

    std::memcpy(static_cast<void*>(address.sun_path), socketPath.data(), socketPath.size());

    return address;
}

} // namespace

AutomationServer::AutomationServer(std::string socketPath)
    : socketPath_(std::move(socketPath)),
      listenDescriptor_(::socket(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0)) {
    const sockaddr_un address = makeSocketAddress(socketPath_);

    // A socket file left by a killed window would make bind fail with EADDRINUSE, and the path carries this
    // process' own id, so removing it first can only ever remove a dead one.
    ::unlink(socketPath_.c_str());

    if (listenDescriptor_ < 0) {
        throwErrno("socket");
    }

    if (::bind(listenDescriptor_, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) < 0) {
        ::close(listenDescriptor_);
        throwErrno("bind");
    }

    if (::listen(listenDescriptor_, kListenBacklog) < 0) {
        ::close(listenDescriptor_);
        throwErrno("listen");
    }
}

AutomationServer::~AutomationServer() noexcept {
    dropClient();
    ::close(listenDescriptor_);
    ::unlink(socketPath_.c_str());
}

const std::string& AutomationServer::socketPath() const noexcept { return socketPath_; }

void AutomationServer::acceptPendingClient() {
    const int accepted = ::accept4(listenDescriptor_, nullptr, nullptr, SOCK_NONBLOCK | SOCK_CLOEXEC);

    if (accepted >= 0) {
        clientDescriptor_ = accepted;
        received_.clear();
    }
}

void AutomationServer::readPendingBytes() {
    std::array<char, kReadChunkBytes> chunk{};
    const ssize_t readBytes = ::recv(clientDescriptor_, chunk.data(), chunk.size(), 0);

    if (readBytes > 0) {
        received_.append(chunk.data(), static_cast<size_t>(readBytes));

        return;
    }

    // Zero is an orderly shutdown and anything else that is not "nothing has arrived yet" is a broken
    // connection; both mean the driver is gone and the next one starts with an empty buffer.
    if (readBytes == 0 || (errno != EAGAIN && errno != EWOULDBLOCK)) {
        dropClient();
    }
}

void AutomationServer::dropClient() {
    if (clientDescriptor_ >= 0) {
        ::close(clientDescriptor_);
        clientDescriptor_ = -1;
    }

    received_.clear();
}

std::optional<std::string> AutomationServer::takeRequestLine() {
    if (clientDescriptor_ < 0) {
        acceptPendingClient();
    }

    if (clientDescriptor_ < 0) {
        return std::nullopt;
    }

    if (received_.find(kLineTerminator) == std::string::npos) {
        readPendingBytes();
    }

    const size_t terminator = received_.find(kLineTerminator);

    if (terminator == std::string::npos) {
        return std::nullopt;
    }

    std::string line = received_.substr(0, terminator);

    received_.erase(0, terminator + 1);

    return line;
}

void AutomationServer::sendResponse(const std::string& line) {
    if (clientDescriptor_ < 0) {
        return;
    }

    // MSG_NOSIGNAL rather than a SIGPIPE handler: a driver that timed out and closed the socket must not take
    // the window down with it, which is exactly what HangForTesting's timeout case produces.
    if (::send(clientDescriptor_, line.data(), line.size(), MSG_NOSIGNAL) < 0) {
        dropClient();
    }
}

std::string defaultAutomationSocketPath() {
    const char* runtimeDirectory = std::getenv("XDG_RUNTIME_DIR");
    const std::string directory = runtimeDirectory == nullptr ? "/tmp" : runtimeDirectory;

    return directory + "/rnl-automation-" + std::to_string(::getpid()) + ".sock";
}

} // namespace react_native_linux
