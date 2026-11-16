#include "ClipboardTransfers.h"

#include "Clipboard.h"

#include <array>
#include <atomic>
#include <cerrno>
#include <csignal>
#include <cstddef>
#include <cstdint>
#include <fcntl.h>
#include <gtest/gtest.h>
#include <poll.h>
#include <string>
#include <thread>
#include <unistd.h>
#include <vector>

namespace {

using react_native_linux::beginClipboardReceive;
using react_native_linux::clipboardText;
using react_native_linux::ClipboardTransfers;
using react_native_linux::receiveClipboardText;
using react_native_linux::setClipboardText;
using react_native_linux::takeClipboardPublish;

constexpr size_t kLargerThanAPipe = 256 * 1024;
constexpr int kContendedRounds = 2000;

struct Pipe {
    int readEnd;
    int writeEnd;
};

Pipe openPipe() {
    std::array<int, 2> descriptors{};

    EXPECT_EQ(pipe2(descriptors.data(), O_CLOEXEC), 0);

    return {.readEnd = descriptors[0], .writeEnd = descriptors[1]};
}

uint64_t receiveRevision() {
    setClipboardText("before the selection");
    takeClipboardPublish();

    return beginClipboardReceive();
}

bool isOpen(int descriptor) { return fcntl(descriptor, F_GETFD) != -1; }

std::vector<pollfd> pollDescriptors(const ClipboardTransfers& transfers) {
    std::vector<pollfd> descriptors;

    transfers.appendPollDescriptors(descriptors);

    return descriptors;
}

void writeAll(int descriptor, const std::string& text) {
    EXPECT_EQ(write(descriptor, text.data(), text.size()), static_cast<ssize_t>(text.size()));
}

std::string readAvailable(int descriptor) {
    std::array<char, 4096> chunk{};
    std::string text;
    ssize_t received = 0;

    while ((received = read(descriptor, chunk.data(), chunk.size())) > 0) {
        text.append(chunk.data(), static_cast<size_t>(received));
    }

    return text;
}

/** #60: a selection is read as the source writes it, never waiting, and lands only at end of file. */
TEST(ClipboardTransfersTest, AReadCompletesAcrossWakesAndStoresAtEndOfFile) {
    ClipboardTransfers transfers;
    const Pipe pipe = openPipe();

    transfers.startRead(pipe.readEnd, receiveRevision());
    ASSERT_EQ(pollDescriptors(transfers).size(), 1U);
    EXPECT_EQ(pollDescriptors(transfers).front().fd, pipe.readEnd);
    EXPECT_EQ(pollDescriptors(transfers).front().events, POLLIN);

    transfers.advance();
    writeAll(pipe.writeEnd, "pasted ");
    transfers.advance();

    EXPECT_EQ(clipboardText(), "");

    writeAll(pipe.writeEnd, "later");
    close(pipe.writeEnd);
    transfers.advance();

    EXPECT_EQ(clipboardText(), "pasted later");
    EXPECT_TRUE(pollDescriptors(transfers).empty());
    EXPECT_FALSE(isOpen(pipe.readEnd));
}

/** #60: a newer selection abandons the read of the one before, so only its own text can land. */
TEST(ClipboardTransfersTest, ANewerSelectionAbandonsThePendingRead) {
    ClipboardTransfers transfers;
    const Pipe older = openPipe();
    const Pipe newer = openPipe();

    transfers.startRead(older.readEnd, receiveRevision());
    transfers.startRead(newer.readEnd, receiveRevision());

    EXPECT_FALSE(isOpen(older.readEnd));

    writeAll(newer.writeEnd, "newer");
    close(newer.writeEnd);
    close(older.writeEnd);
    transfers.advance();

    EXPECT_EQ(clipboardText(), "newer");
}

/** #60: a copy made while a selection is still being read wins over that selection's text. */
TEST(ClipboardTransfersTest, ACopyMadeDuringAReadIsNotOverwrittenByIt) {
    ClipboardTransfers transfers;
    const Pipe pipe = openPipe();

    transfers.startRead(pipe.readEnd, receiveRevision());
    setClipboardText("copied here");
    writeAll(pipe.writeEnd, "from another application");
    close(pipe.writeEnd);
    transfers.advance();

    EXPECT_EQ(clipboardText(), "copied here");
    EXPECT_TRUE(takeClipboardPublish());
    EXPECT_TRUE(pollDescriptors(transfers).empty());
}

/** #60: a read that fails is dropped, leaving the selection's paste empty rather than stale. */
TEST(ClipboardTransfersTest, AFailedReadLeavesTheClipboardEmpty) {
    ClipboardTransfers transfers;
    const int directory = open("/", O_RDONLY | O_DIRECTORY | O_CLOEXEC);

    transfers.startRead(directory, receiveRevision());
    transfers.advance();

    EXPECT_EQ(clipboardText(), "");
    EXPECT_TRUE(pollDescriptors(transfers).empty());
}

/** #60: text larger than the pipe goes out a pipeful per wake, then the pipe is closed. */
TEST(ClipboardTransfersTest, AWriteLargerThanThePipeCompletesAcrossWakes) {
    ClipboardTransfers transfers;
    const Pipe pipe = openPipe();
    const std::string text(kLargerThanAPipe, 'x');
    std::string received;

    fcntl(pipe.readEnd, F_SETFL, O_NONBLOCK);
    transfers.startWrite(pipe.writeEnd, text);
    ASSERT_EQ(pollDescriptors(transfers).size(), 1U);
    EXPECT_EQ(pollDescriptors(transfers).front().events, POLLOUT);

    while (!pollDescriptors(transfers).empty()) {
        transfers.advance();
        received += readAvailable(pipe.readEnd);
    }

    received += readAvailable(pipe.readEnd);

    EXPECT_EQ(received, text);
    EXPECT_FALSE(isOpen(pipe.writeEnd));
    close(pipe.readEnd);
}

/** #60: a reader that closes its pipe early costs the write an EPIPE, not the process its SIGPIPE. */
TEST(ClipboardTransfersTest, AReaderThatLeftEndsTheWriteWithoutSignallingTheProcess) {
    ClipboardTransfers transfers;
    const Pipe pipe = openPipe();
    const auto previousHandler = std::signal(SIGPIPE, SIG_DFL);
    sigset_t pending{};

    close(pipe.readEnd);
    transfers.startWrite(pipe.writeEnd, "nobody reads this");
    transfers.advance();
    sigpending(&pending);
    std::signal(SIGPIPE, previousHandler);

    EXPECT_TRUE(pollDescriptors(transfers).empty());
    EXPECT_FALSE(isOpen(pipe.writeEnd));
    EXPECT_EQ(sigismember(&pending, SIGPIPE), 0);
}

/** #60: transfers still running when the clipboard goes away are closed with it. */
TEST(ClipboardTransfersTest, DestructionClosesEveryPendingTransfer) {
    const Pipe reading = openPipe();
    const Pipe writing = openPipe();

    {
        ClipboardTransfers transfers;

        transfers.startRead(reading.readEnd, receiveRevision());
        transfers.startWrite(writing.writeEnd, std::string(kLargerThanAPipe, 'x'));
        transfers.advance();
    }

    EXPECT_FALSE(isOpen(reading.readEnd));
    EXPECT_FALSE(isOpen(writing.writeEnd));
    close(reading.writeEnd);
    close(writing.readEnd);
}

/**
 * #60, AGENTS.md rule 6: the JavaScript thread's `setString` and `getString` race the frame thread's publish and
 * receive on the one clipboard, under TSan in CI. After a copy, the JavaScript thread reads that copy, the empty
 * clipboard of a selection being received, or that selection's text, never an earlier copy.
 */
TEST(ClipboardTransfersTest, TheJavaScriptAndFrameThreadsShareTheClipboardSafely) {
    std::atomic<bool> isCopying{true};
    std::thread javaScriptThread([&isCopying] {
        for (int round = 0; round < kContendedRounds; ++round) {
            const std::string copy = "copy " + std::to_string(round);

            setClipboardText(copy);

            const std::string pasted = clipboardText();

            EXPECT_TRUE(pasted == copy || pasted.empty() || pasted == "received") << pasted;
        }

        isCopying = false;
    });

    while (isCopying) {
        takeClipboardPublish();

        receiveClipboardText("received", beginClipboardReceive());
    }

    javaScriptThread.join();
}

} // namespace
