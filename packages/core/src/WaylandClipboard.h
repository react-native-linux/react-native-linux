#pragma once

#include "ClipboardTransfers.h"

#include <cstdint>
#include <string>
#include <vector>

struct wl_data_device;
struct wl_data_device_listener;
struct wl_data_device_manager;
struct wl_data_offer;
struct wl_data_offer_listener;
struct wl_data_source;
struct wl_data_source_listener;
struct wl_seat;
struct wl_surface;

namespace react_native_linux {

class WaylandSerialLedger;

/**
 * The seat's `wl_data_device` selection, kept in step with the process clipboard of `Clipboard.h` (#60).
 *
 * Copy: `publishPendingText` offers what `setClipboardText` stored as a `wl_data_source` in the
 * `kClipboardTextMimeTypes`, plus a private type naming this process, with the `Selection` serial from the
 * ledger. Every `send` becomes a write in `transfers`. A `cancelled` source is another client's copy taking over.
 *
 * Paste: a `selection` from another client empties the clipboard and is read **eagerly**, as soon as it arrives,
 * in the type `preferredClipboardMimeType` picks, so Ctrl+V and `RNCClipboard`'s `getString` read the process
 * clipboard without a round trip. A selection that offers this process's private type is its own copy coming
 * back and is not read. A selection without text leaves the clipboard empty; no selection at all empties it only
 * when no copy made here is still waiting to be published.
 *
 * Every transfer is a non-blocking pipe in `transfers`, which `WaylandWindow` polls and advances in its event
 * loop, so a source or reader that stalls never holds the frame thread.
 *
 * Out of scope here and still #60's: the primary selection, and drag-and-drop, whose offers are declined.
 *
 * Threading contract: every member runs on the frame thread that owns the Wayland connection; the listeners are
 * invoked from inside its dispatch. The process clipboard it reads and writes is the only state shared with the
 * JavaScript thread, behind that clipboard's own mutex.
 */
class WaylandClipboard final {
public:
    WaylandClipboard(wl_data_device_manager* manager, wl_seat* seat, const WaylandSerialLedger& serialLedger);
    WaylandClipboard(const WaylandClipboard&) = delete;
    WaylandClipboard(WaylandClipboard&&) = delete;
    WaylandClipboard& operator=(const WaylandClipboard&) = delete;
    WaylandClipboard& operator=(WaylandClipboard&&) = delete;
    ~WaylandClipboard() noexcept;

    /** Called once per frame: offers this frame's copy, if there was one, as the seat's selection. */
    void publishPendingText();
    ClipboardTransfers& transfers() noexcept;

private:
    void receiveSelection(wl_data_offer* offer);

    static const wl_data_device_listener kDataDeviceListener;
    static const wl_data_offer_listener kDataOfferListener;
    static const wl_data_source_listener kDataSourceListener;

    static void handleDataOffer(void* data, wl_data_device* dataDevice, wl_data_offer* offer);
    static void handleEnter(void* data, wl_data_device* dataDevice, uint32_t serial, wl_surface* surface,
                            int32_t surfaceX, int32_t surfaceY, wl_data_offer* offer);
    static void handleLeave(void* data, wl_data_device* dataDevice);
    static void handleMotion(void* data, wl_data_device* dataDevice, uint32_t time, int32_t surfaceX, int32_t surfaceY);
    static void handleDrop(void* data, wl_data_device* dataDevice);
    static void handleSelection(void* data, wl_data_device* dataDevice, wl_data_offer* offer);
    static void handleOfferMimeType(void* data, wl_data_offer* offer, const char* mimeType);
    static void handleSourceTarget(void* data, wl_data_source* source, const char* mimeType);
    static void handleSourceSend(void* data, wl_data_source* source, const char* mimeType, int32_t descriptor);
    static void handleSourceCancelled(void* data, wl_data_source* source);

    wl_data_device_manager* manager_;
    wl_data_device* dataDevice_;
    const WaylandSerialLedger& serialLedger_;
    std::string ownMimeType_;
    /** The latest source set as the selection, destroyed when cancelled or with the clipboard. */
    wl_data_source* ownedSource_{nullptr};
    /** The most recently introduced offer and the types it has advertised so far; offers arrive one at a time. */
    wl_data_offer* introducedOffer_{nullptr};
    std::vector<std::string> introducedMimeTypes_;
    ClipboardTransfers transfers_;
};

} // namespace react_native_linux
