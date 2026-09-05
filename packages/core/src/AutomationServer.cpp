#include "AutomationServer.h"

#include <array>
#include <cerrno>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <optional>
#include <poll.h>
#include <string>
#include <string_view>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <system_error>
#include <unistd.h>
#include <utility>

namespace react_native_linux {

namespace {

constexpr int kListenBacklog = 1;
constexpr int kWritableTimeoutMilliseconds = 2000;
constexpr size_t kReadChunkBytes = 4096;

// The errno is passed in rather than read here, because every caller has a ::close or a ::unlink to do first
// and either of those may set it: a cleanup that succeeded would otherwise rename the failure that caused it.
[[noreturn]] void throwErrno(const char* syscallName, int failure) {
    throw std::system_error(failure, std::generic_category(), syscallName);
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
    // Before anything else that can set errno: ::unlink normally fails with ENOENT on a clean start, and
    // throwErrno reads the global, so a failed ::socket has to be reported before that overwrites it.
    if (listenDescriptor_ < 0) {
        throwErrno("socket", errno);
    }

    // A throw from here escapes the constructor, so the destructor never runs and the descriptor would leak.
    sockaddr_un address{};

    try {
        address = makeSocketAddress(socketPath_);
    } catch (...) {
        ::close(listenDescriptor_);

        throw;
    }

    // A socket file left by a killed window would make bind fail with EADDRINUSE, and the path carries this
    // process' own id, so removing it first can only ever remove a dead one.
    ::unlink(socketPath_.c_str());

    if (::bind(listenDescriptor_, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) < 0) {
        const int failure = errno;

        // No unlink: bind is what would have created the file, so there is nothing here this process made.
        ::close(listenDescriptor_);
        throwErrno("bind", failure);
    }

    // Owner-only, after the file exists and before anything can connect to it. `XDG_RUNTIME_DIR` is already
    // 0700, but the `/tmp` fallback is not, and a socket left group- or world-accessible there lets any other
    // local account drive this window — `DumpVisualTree` reads the app's tree and `MarkTestPassed` reports on
    // it. The mode is set rather than left to the umask, because a permissive umask is exactly the case that
    // makes the fallback reachable. `chmod` on the path, not `fchmod` on the descriptor: POSIX leaves the
    // latter unspecified for sockets and it does nothing on Linux once the socket is bound.
    if (::chmod(socketPath_.c_str(), S_IRUSR | S_IWUSR) < 0) {
        abandonBoundSocket("chmod");
    }

    if (::listen(listenDescriptor_, kListenBacklog) < 0) {
        abandonBoundSocket("listen");
    }
}

void AutomationServer::abandonBoundSocket(const char* syscallName) {
    const int failure = errno;

    ::close(listenDescriptor_);
    ::unlink(socketPath_.c_str());

    throwErrno(syscallName, failure);
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
        // A client that never sends a newline would otherwise grow the buffer for as long as the window runs.
        // It is told why before the socket closes, because a driver that reads its answer learns more from a
        // refusal than from a connection that simply ended.
        if (!received_.append(std::string_view(chunk.data(), static_cast<size_t>(readBytes)))) {
            sendResponse(formatAutomationFailure("a request may not exceed " + std::to_string(kMaxRequestBytes) +
                                                 " bytes without a newline"));
            dropClient();
        }

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

    std::optional<std::string> line = received_.takeLine();

    if (line.has_value()) {
        return line;
    }

    readPendingBytes();

    return received_.takeLine();
}

bool AutomationServer::waitUntilWritable() const {
    pollfd writable{.fd = clientDescriptor_, .events = POLLOUT, .revents = 0};

    return ::poll(&writable, 1, kWritableTimeoutMilliseconds) > 0;
}

void AutomationServer::sendResponse(const std::string& line) {
    if (clientDescriptor_ < 0) {
        return;
    }

    size_t sent = 0;

    // The client is nonblocking, so a response larger than the send buffer — which a DumpVisualTree of a real
    // tree can be — goes out in pieces. A partial write that is not finished is worse than no write at all: the
    // driver reads a line with no terminator on it and waits for the rest until its own deadline passes.
    while (sent < line.size()) {
        // MSG_NOSIGNAL rather than a SIGPIPE handler: a driver that timed out and closed the socket must not
        // take the window down with it, which is exactly what HangForTesting's timeout case produces.
        const ssize_t written = ::send(clientDescriptor_, line.data() + sent, line.size() - sent, MSG_NOSIGNAL);

        if (written > 0) {
            sent += static_cast<size_t>(written);

            continue;
        }

        if (errno == EINTR) {
            continue;
        }

        // A full send buffer is a driver that has not read yet, not a driver that has gone: wait for it to drain
        // and carry on. Only a poll that times out or fails gives up, so a driver that stopped reading entirely
        // cannot hold the frame thread for longer than that.
        if ((errno == EAGAIN || errno == EWOULDBLOCK) && waitUntilWritable()) {
            continue;
        }

        dropClient();

        return;
    }
}

std::string defaultAutomationSocketPath() {
    const char* runtimeDirectory = std::getenv("XDG_RUNTIME_DIR");
    const std::string directory = runtimeDirectory == nullptr ? "/tmp" : runtimeDirectory;

    return directory + "/rnl-automation-" + std::to_string(::getpid()) + ".sock";
}

} // namespace react_native_linux
