#include "WaylandClipboard.h"

#include "Clipboard.h"
#include "WaylandSerialLedger.h"

#include <array>
#include <chrono>
#include <csignal>
#include <cstddef>
#include <cstdint>
#include <fcntl.h>
#include <optional>
#include <poll.h>
#include <string>
#include <string_view>
#include <sys/types.h>
#include <unistd.h>
#include <utility>
#include <wayland-client.h>

namespace react_native_linux {

namespace {

constexpr std::chrono::milliseconds kTransferTimeout{100};
constexpr size_t kReadChunkSize = 4096;

/** Waits for `events` on `descriptor` until `deadline`; false once the deadline passed or the pipe failed. */
bool waitForPipe(int descriptor, short events, std::chrono::steady_clock::time_point deadline) {
    const auto remaining =
        std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now());
    pollfd pipe{.fd = descriptor, .events = events, .revents = 0};

    return remaining.count() > 0 && poll(&pipe, 1, static_cast<int>(remaining.count())) > 0;
}

void writeText(int descriptor, std::string_view text) {
    const auto deadline = std::chrono::steady_clock::now() + kTransferTimeout;

    // The descriptor's blocking mode is this client's to choose; a reader that stops reading must not hold the
    // frame thread inside write().
    fcntl(descriptor, F_SETFL, fcntl(descriptor, F_GETFL) | O_NONBLOCK);

    while (!text.empty() && waitForPipe(descriptor, POLLOUT, deadline)) {
        const ssize_t written = write(descriptor, text.data(), text.size());

        if (written < 0) {
            break;
        }

        text.remove_prefix(static_cast<size_t>(written));
    }

    close(descriptor);
}

/** The whole of what the source wrote, or nothing if it did not finish within the timeout. */
std::optional<std::string> readText(int descriptor) {
    const auto deadline = std::chrono::steady_clock::now() + kTransferTimeout;
    std::array<char, kReadChunkSize> chunk{};
    std::string text;

    while (waitForPipe(descriptor, POLLIN, deadline)) {
        const ssize_t received = read(descriptor, chunk.data(), chunk.size());

        if (received <= 0) {
            close(descriptor);

            return received == 0 ? std::optional<std::string>(std::move(text)) : std::nullopt;
        }

        text.append(chunk.data(), static_cast<size_t>(received));
    }

    close(descriptor);

    return std::nullopt;
}

} // namespace

const wl_data_device_listener WaylandClipboard::kDataDeviceListener = [] {
    wl_data_device_listener listener{};

    listener.data_offer = WaylandClipboard::handleDataOffer;
    listener.enter = WaylandClipboard::handleEnter;
    listener.leave = WaylandClipboard::handleLeave;
    listener.motion = WaylandClipboard::handleMotion;
    listener.drop = WaylandClipboard::handleDrop;
    listener.selection = WaylandClipboard::handleSelection;

    return listener;
}();

// Bound at version 1, so the version 3 drag-and-drop action events are never sent and stay null.
const wl_data_offer_listener WaylandClipboard::kDataOfferListener = [] {
    wl_data_offer_listener listener{};

    listener.offer = WaylandClipboard::handleOfferMimeType;

    return listener;
}();

const wl_data_source_listener WaylandClipboard::kDataSourceListener = [] {
    wl_data_source_listener listener{};

    listener.target = WaylandClipboard::handleSourceTarget;
    listener.send = WaylandClipboard::handleSourceSend;
    listener.cancelled = WaylandClipboard::handleSourceCancelled;

    return listener;
}();

WaylandClipboard::WaylandClipboard(wl_display* display, wl_data_device_manager* manager, wl_seat* seat,
                                   const WaylandSerialLedger& serialLedger)
    : display_(display), manager_(manager), dataDevice_(wl_data_device_manager_get_data_device(manager, seat)),
      serialLedger_(serialLedger) {
    // A reader that closes its pipe before the text is written must cost an EPIPE, not the process; GIO and foot
    // ignore the signal the same way.
    std::signal(SIGPIPE, SIG_IGN);
    wl_data_device_add_listener(dataDevice_, &kDataDeviceListener, this);
}

WaylandClipboard::~WaylandClipboard() noexcept {
    if (introducedOffer_ != nullptr) {
        wl_data_offer_destroy(introducedOffer_);
    }

    if (ownedSource_ != nullptr) {
        wl_data_source_destroy(ownedSource_);
    }

    wl_data_device_destroy(dataDevice_);
}

void WaylandClipboard::publishPendingText() {
    const std::optional<std::string> text = takeClipboardPublish();

    if (!text.has_value()) {
        return;
    }

    const std::optional<uint32_t> serial = serialLedger_.requestSerial(WaylandSerialKind::Selection);

    if (!serial.has_value()) {
        return;
    }

    // The source it replaces is not destroyed here: the compositor cancels it, and the handler destroys it then.
    ownedSource_ = wl_data_device_manager_create_data_source(manager_);
    wl_data_source_add_listener(ownedSource_, &kDataSourceListener, this);

    for (const std::string_view mimeType : kClipboardTextMimeTypes) {
        wl_data_source_offer(ownedSource_, std::string(mimeType).c_str());
    }

    wl_data_device_set_selection(dataDevice_, ownedSource_, serial.value());
}

void WaylandClipboard::receiveSelection(wl_data_offer* offer) {
    // `data_offer` introduced this offer immediately before, so the types collected since are its types.
    introducedOffer_ = nullptr;

    if (ownedSource_ != nullptr) {
        // This client's own copy coming back: its text is already the clipboard's.
        if (offer != nullptr) {
            wl_data_offer_destroy(offer);
        }

        return;
    }

    if (offer == nullptr) {
        receiveClipboardText({});

        return;
    }

    const std::optional<std::string_view> mimeType = preferredClipboardMimeType(introducedMimeTypes_);
    std::optional<std::string> text = std::string();

    if (std::array<int, 2> descriptors{}; mimeType.has_value() && pipe2(descriptors.data(), O_CLOEXEC) == 0) {
        wl_data_offer_receive(offer, std::string(mimeType.value()).c_str(), descriptors[1]);
        close(descriptors[1]);
        wl_display_flush(display_);
        text = readText(descriptors[0]);
    }

    wl_data_offer_destroy(offer);

    if (text.has_value()) {
        receiveClipboardText(std::move(text.value()));
    }
}

void WaylandClipboard::handleDataOffer(void* data, wl_data_device* /*dataDevice*/, wl_data_offer* offer) {
    WaylandClipboard* clipboard = static_cast<WaylandClipboard*>(data);

    clipboard->introducedOffer_ = offer;
    clipboard->introducedMimeTypes_.clear();
    wl_data_offer_add_listener(offer, &kDataOfferListener, clipboard);
}

void WaylandClipboard::handleEnter(void* data, wl_data_device* /*dataDevice*/, uint32_t /*serial*/,
                                   wl_surface* /*surface*/, int32_t /*surfaceX*/, int32_t /*surfaceY*/,
                                   wl_data_offer* offer) {
    WaylandClipboard* clipboard = static_cast<WaylandClipboard*>(data);

    // Drag-and-drop is out of #60's scope, so a drag's offer is declined by never accepting a type from it.
    if (offer != nullptr && offer == clipboard->introducedOffer_) {
        clipboard->introducedOffer_ = nullptr;
        wl_data_offer_destroy(offer);
    }
}

void WaylandClipboard::handleLeave(void* /*data*/, wl_data_device* /*dataDevice*/) {}

void WaylandClipboard::handleMotion(void* /*data*/, wl_data_device* /*dataDevice*/, uint32_t /*time*/,
                                    int32_t /*surfaceX*/, int32_t /*surfaceY*/) {}

void WaylandClipboard::handleDrop(void* /*data*/, wl_data_device* /*dataDevice*/) {}

void WaylandClipboard::handleSelection(void* data, wl_data_device* /*dataDevice*/, wl_data_offer* offer) {
    static_cast<WaylandClipboard*>(data)->receiveSelection(offer);
}

void WaylandClipboard::handleOfferMimeType(void* data, wl_data_offer* offer, const char* mimeType) {
    WaylandClipboard* clipboard = static_cast<WaylandClipboard*>(data);

    if (offer == clipboard->introducedOffer_) {
        clipboard->introducedMimeTypes_.emplace_back(mimeType);
    }
}

void WaylandClipboard::handleSourceTarget(void* /*data*/, wl_data_source* /*source*/, const char* /*mimeType*/) {}

void WaylandClipboard::handleSourceSend(void* /*data*/, wl_data_source* /*source*/, const char* /*mimeType*/,
                                        int32_t descriptor) {
    writeText(descriptor, clipboardText());
}

void WaylandClipboard::handleSourceCancelled(void* data, wl_data_source* source) {
    WaylandClipboard* clipboard = static_cast<WaylandClipboard*>(data);

    if (source == clipboard->ownedSource_) {
        clipboard->ownedSource_ = nullptr;
    }

    wl_data_source_destroy(source);
}

} // namespace react_native_linux
