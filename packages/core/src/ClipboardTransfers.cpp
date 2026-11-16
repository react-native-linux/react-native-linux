#include "ClipboardTransfers.h"

#include "Clipboard.h"

#include <algorithm>
#include <array>
#include <cerrno>
#include <csignal>
#include <cstddef>
#include <cstdint>
#include <ctime>
#include <fcntl.h>
#include <iostream>
#include <poll.h>
#include <pthread.h>
#include <string>
#include <string_view>
#include <sys/types.h>
#include <unistd.h>
#include <utility>
#include <vector>

namespace react_native_linux {

namespace {

constexpr size_t kReadChunkSize = 4096;

void makeNonBlocking(int descriptor) { fcntl(descriptor, F_SETFL, fcntl(descriptor, F_GETFL) | O_NONBLOCK); }

/**
 * `write` with SIGPIPE blocked on the calling thread for the duration of the call only, so a reader that closed
 * its pipe costs an `EPIPE` rather than the process, and neither the process's disposition nor any thread's mask
 * is left changed for a child such as `xdg-open` to inherit. The signal an `EPIPE` raised stays pending while it
 * is blocked, so it is consumed before the mask is restored.
 */
ssize_t writeWithoutSigpipe(int descriptor, std::string_view text) {
    sigset_t sigpipe{};
    sigset_t previousMask{};

    sigemptyset(&sigpipe);
    sigaddset(&sigpipe, SIGPIPE);
    pthread_sigmask(SIG_BLOCK, &sigpipe, &previousMask);

    const ssize_t written = write(descriptor, text.data(), text.size());
    const int writeErrno = errno;

    if (written < 0 && writeErrno == EPIPE) {
        const timespec immediately{};

        sigtimedwait(&sigpipe, nullptr, &immediately);
    }

    pthread_sigmask(SIG_SETMASK, &previousMask, nullptr);
    errno = writeErrno;

    return written;
}

} // namespace

ClipboardTransfers::~ClipboardTransfers() noexcept {
    cancelRead();

    for (const PendingWrite& pendingWrite : writes_) {
        close(pendingWrite.descriptor);
    }
}

void ClipboardTransfers::startRead(int descriptor, uint64_t revision) {
    cancelRead();
    makeNonBlocking(descriptor);
    read_ = PendingRead{.descriptor = descriptor, .revision = revision, .text = {}};
}

void ClipboardTransfers::cancelRead() noexcept {
    if (read_.has_value()) {
        close(read_->descriptor);
        read_.reset();
    }
}

void ClipboardTransfers::startWrite(int descriptor, std::string text) {
    makeNonBlocking(descriptor);
    writes_.push_back(PendingWrite{.descriptor = descriptor, .text = std::move(text), .offset = 0});
}

void ClipboardTransfers::appendPollDescriptors(std::vector<pollfd>& descriptors) const {
    if (read_.has_value()) {
        descriptors.push_back(pollfd{.fd = read_->descriptor, .events = POLLIN, .revents = 0});
    }

    for (const PendingWrite& pendingWrite : writes_) {
        descriptors.push_back(pollfd{.fd = pendingWrite.descriptor, .events = POLLOUT, .revents = 0});
    }
}

void ClipboardTransfers::advance() {
    std::array<char, kReadChunkSize> chunk{};
    ssize_t received = 0;

    while (read_.has_value() && (received = read(read_->descriptor, chunk.data(), chunk.size())) > 0) {
        read_->text.append(chunk.data(), static_cast<size_t>(received));
    }

    if (read_.has_value() && received == 0) {
        const size_t byteCount = read_->text.size();

        if (receiveClipboardText(std::move(read_->text), read_->revision)) {
            std::cerr << "[rnl-clipboard] received a selection of " << byteCount << " bytes" << std::endl;
        }
    }

    if (read_.has_value() && (received == 0 || errno != EAGAIN)) {
        cancelRead();
    }

    std::erase_if(writes_, [](PendingWrite& pendingWrite) {
        ssize_t written = 0;

        while (pendingWrite.offset < pendingWrite.text.size() &&
               (written = writeWithoutSigpipe(pendingWrite.descriptor,
                                              std::string_view(pendingWrite.text).substr(pendingWrite.offset))) > 0) {
            pendingWrite.offset += static_cast<size_t>(written);
        }

        const bool isFinished = pendingWrite.offset == pendingWrite.text.size() || errno != EAGAIN;

        if (isFinished) {
            close(pendingWrite.descriptor);
        }

        return isFinished;
    });
}

} // namespace react_native_linux
