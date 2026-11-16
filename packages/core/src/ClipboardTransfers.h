#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <poll.h>
#include <string>
#include <vector>

namespace react_native_linux {

/**
 * The pipe transfers behind the Wayland selection (#60), none of which ever blocks the frame thread: the event
 * loop polls `appendPollDescriptors` beside the display, and `advance` moves each transfer as far as its pipe
 * allows without waiting.
 *
 * A read is another application's selection. At end of file its text becomes the clipboard through
 * `receiveClipboardText` under the revision the read started with, so a copy made here in the meantime is never
 * overwritten, and the window traces `[rnl-clipboard] received a selection of <n> bytes`. There is one read at a
 * time: a newer selection abandons the pending one, and so does destruction. A write is one `send` request: the
 * text it was given, written into the reader's pipe and closed once written or once the reader has gone.
 *
 * Threading contract: frame thread only. The clipboard a read stores into is the state shared with the
 * JavaScript thread, behind that clipboard's own mutex.
 */
class ClipboardTransfers final {
public:
    ClipboardTransfers() = default;
    ClipboardTransfers(const ClipboardTransfers&) = delete;
    ClipboardTransfers(ClipboardTransfers&&) = delete;
    ClipboardTransfers& operator=(const ClipboardTransfers&) = delete;
    ClipboardTransfers& operator=(ClipboardTransfers&&) = delete;
    ~ClipboardTransfers() noexcept;

    /** Reads `descriptor`, which it now owns, to end of file; abandons the read already pending. */
    void startRead(int descriptor, uint64_t revision);
    void cancelRead() noexcept;
    /** Writes `text` into `descriptor`, which it now owns, then closes it. */
    void startWrite(int descriptor, std::string text);

    void appendPollDescriptors(std::vector<pollfd>& descriptors) const;
    void advance();

private:
    struct PendingRead {
        int descriptor;
        uint64_t revision;
        std::string text;
    };

    struct PendingWrite {
        int descriptor;
        std::string text;
        size_t offset;
    };

    std::optional<PendingRead> read_;
    std::vector<PendingWrite> writes_;
};

} // namespace react_native_linux
