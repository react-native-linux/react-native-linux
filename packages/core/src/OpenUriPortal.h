#pragma once

#include "SdBus.h"

#include <chrono>
#include <filesystem>
#include <memory>
#include <systemd/sd-bus.h>

namespace react_native_linux {

inline constexpr std::chrono::microseconds kPortalCallTimeout = std::chrono::seconds(5);

/**
 * expo-sharing's desktop mechanism (#744, ADR-0004): the xdg-desktop-portal's `org.freedesktop.portal.OpenURI` on the
 * session bus, through sd-bus as `SecretService` uses it. There is no freedesktop share sheet; `OpenFile` with `ask`
 * set is the nearest thing, the portal's own chooser of the applications that open the file.
 *
 * One connection carries every call. It opens at the first call, opens again once the last one has closed, and stays
 * open until this object goes, because the portal closes a request, and dismisses its chooser, as soon as the caller
 * that made it leaves the bus. Each call gives up after `timeout`, so a stalled portal never hangs the caller. Opening
 * the connection is bounded by sd-bus's default 25 s call timeout instead, since `sd_bus_open_user` sends its `Hello`
 * before the connection has a timeout of its own: the first call to a bus that never answers takes about 30 s.
 *
 * Threading contract: one thread at a time. `FileSharing`'s worker is the only caller, so the JavaScript thread never
 * waits on the bus, and `WindowSession` lets this object go only after that worker has joined.
 */
class OpenUriPortal final {
public:
    explicit OpenUriPortal(SdBusOpener openBus = sd_bus_open_user,
                           std::chrono::microseconds timeout = kPortalCallTimeout);

    /**
     * Whether the session bus has a portal that serves `OpenURI`, which it shows by answering the interface's
     * `version`. No session bus, no portal, a portal without the interface, or no answer within `timeout` is `false`.
     */
    [[nodiscard]] bool isAvailable();

    /**
     * Asks the portal to open `file` in the application the user picks, handing it over as a read-only descriptor,
     * and returns once the portal has accepted the request. The portal reports the user's choice, or a dismissed
     * chooser, only to a caller that waits for its `Response` signal, and this one does not: like Android's chooser,
     * sharing does not say where the file went. Throws when `file` is not a regular file that can be opened, the bus
     * cannot be reached, or the portal refuses or does not answer within `timeout`.
     */
    void openFile(const std::filesystem::path& file);

private:
    struct BusClose {
        void operator()(sd_bus* bus) const noexcept;
    };

    /** The open connection, or null with `openBus`'s negative errno in `result`. */
    sd_bus* connection(int& result);

    SdBusOpener openBus_;
    std::chrono::microseconds timeout_;
    std::unique_ptr<sd_bus, BusClose> bus_;
};

} // namespace react_native_linux
