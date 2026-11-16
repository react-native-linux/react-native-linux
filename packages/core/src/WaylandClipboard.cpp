#include "WaylandClipboard.h"

#include "Clipboard.h"
#include "WaylandSerialLedger.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <fcntl.h>
#include <optional>
#include <string>
#include <string_view>
#include <unistd.h>
#include <wayland-client.h>

namespace react_native_linux {

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

WaylandClipboard::WaylandClipboard(wl_data_device_manager* manager, wl_seat* seat,
                                   const WaylandSerialLedger& serialLedger)
    : manager_(manager), dataDevice_(wl_data_device_manager_get_data_device(manager, seat)),
      serialLedger_(serialLedger), ownMimeType_("application/x-react-native-linux-" + std::to_string(getpid())) {
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
    // Read rather than requested: a copy made before the first press waits for one, without a refusal logged per
    // frame, and stays pending until then.
    const uint32_t serial = serialLedger_.serial(WaylandSerialKind::Selection);

    if (serial == 0 || !takeClipboardPublish()) {
        return;
    }

    // The source it replaces is not destroyed here: the compositor cancels it, and the handler destroys it then.
    ownedSource_ = wl_data_device_manager_create_data_source(manager_);
    wl_data_source_add_listener(ownedSource_, &kDataSourceListener, this);

    for (const std::string_view mimeType : kClipboardTextMimeTypes) {
        wl_data_source_offer(ownedSource_, std::string(mimeType).c_str());
    }

    wl_data_source_offer(ownedSource_, ownMimeType_.c_str());
    wl_data_device_set_selection(dataDevice_, ownedSource_, serial);
}

ClipboardTransfers& WaylandClipboard::transfers() noexcept { return transfers_; }

void WaylandClipboard::receiveSelection(wl_data_offer* offer) {
    // `data_offer` introduced this offer immediately before, so the types collected since are its types.
    introducedOffer_ = nullptr;
    transfers_.cancelRead();

    if (offer == nullptr) {
        beginClipboardReceive();

        return;
    }

    // This client's own copy coming back is recognised by the type only its sources offer, not by still holding a
    // source: a compositor that ignores a `set_selection` sends no `cancelled` for it.
    const bool isOwnCopy = std::ranges::find(introducedMimeTypes_, ownMimeType_) != introducedMimeTypes_.end();
    const std::optional<std::string_view> mimeType = preferredClipboardMimeType(introducedMimeTypes_);
    const std::optional<uint64_t> revision = isOwnCopy ? std::nullopt : beginClipboardReceive();

    if (std::array<int, 2> descriptors{};
        revision.has_value() && mimeType.has_value() && pipe2(descriptors.data(), O_CLOEXEC) == 0) {
        wl_data_offer_receive(offer, std::string(mimeType.value()).c_str(), descriptors[1]);
        close(descriptors[1]);
        transfers_.startRead(descriptors[0], revision.value());
    }

    wl_data_offer_destroy(offer);
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

void WaylandClipboard::handleSourceSend(void* data, wl_data_source* /*source*/, const char* /*mimeType*/,
                                        int32_t descriptor) {
    static_cast<WaylandClipboard*>(data)->transfers_.startWrite(descriptor, clipboardText());
}

void WaylandClipboard::handleSourceCancelled(void* data, wl_data_source* source) {
    WaylandClipboard* clipboard = static_cast<WaylandClipboard*>(data);

    if (source == clipboard->ownedSource_) {
        clipboard->ownedSource_ = nullptr;
    }

    wl_data_source_destroy(source);
}

} // namespace react_native_linux
