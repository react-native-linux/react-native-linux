#pragma once

#include "Appearance.h"

#include <systemd/sd-bus.h>

#include <memory>
#include <optional>

namespace react_native_linux {

/**
 * `org.freedesktop.appearance color-scheme` read from `org.freedesktop.portal.Settings` over the session bus:
 * the colour scheme the desktop is actually in, and the `SettingChanged` signal that says it moved.
 *
 * The client is **sd-bus**, from libsystemd. Nothing in this tree linked a D-Bus client before this file, so the
 * choice was open: libdbus needs its own main-loop integration and a hand-written marshalling layer, GDBus drags
 * in GLib and a `GMainContext` this platform has no other use for, and `sdbus-c++` is a third-party wrapper over
 * the same sd-bus. sd-bus is already on every systemd host, is one `pkg_check_modules(libsystemd)`, and is the
 * only one of the four whose event loop can be *pumped* rather than *owned* — which is what lets the whole
 * connection live on the frame thread.
 *
 * That is the threading contract, and it is why there is no dispatch thread and no mutex here.
 * `processPendingSignals` calls `sd_bus_process` until the connection has nothing left, from
 * `WindowSession::deliverInput` — the same once-per-frame place `publishPendingDimensions` is called from. The
 * match callback therefore runs on the frame thread, and `AppearanceModel`, which is unsynchronised frame-thread
 * state, is only ever written from there. A portal signal reaches JavaScript exactly as a compositor configure
 * does: recorded on the frame thread, published to the JavaScript thread through the module's `CallInvoker`.
 *
 * When there is no session bus, no `org.freedesktop.portal.Desktop` on it, or a portal too old for
 * `ReadOne` — added in xdg-desktop-portal 1.15 — construction leaves `initialColorScheme` empty and every
 * `processPendingSignals` is a no-op. The caller keeps `kFallbackColorScheme`; an app on a portal-less desktop
 * runs in light mode rather than failing to start.
 */
class AppearancePortal final {
public:
    AppearancePortal();
    AppearancePortal(const AppearancePortal&) = delete;
    AppearancePortal(AppearancePortal&&) = delete;
    AppearancePortal& operator=(const AppearancePortal&) = delete;
    AppearancePortal& operator=(AppearancePortal&&) = delete;
    ~AppearancePortal() noexcept = default;

    /**
     * What the portal answered at construction, or nothing when it did not answer or said "no preference".
     */
    std::optional<ColorScheme> initialColorScheme() const noexcept;

    /**
     * Drains the bus and applies whatever `SettingChanged` carried, on the calling thread. Called once per frame.
     */
    void processPendingSignals(AppearanceModel& appearanceModel);

private:
    struct BusDeleter {
        void operator()(sd_bus* bus) const noexcept;
    };

    struct SlotDeleter {
        void operator()(sd_bus_slot* slot) const noexcept;
    };

    static int onSettingChanged(sd_bus_message* message, void* userData, sd_bus_error* error);

    std::unique_ptr<sd_bus, BusDeleter> bus_;
    std::unique_ptr<sd_bus_slot, SlotDeleter> settingChangedSlot_;
    std::optional<ColorScheme> initialColorScheme_;
    std::optional<ColorScheme> signalledColorScheme_;
};

} // namespace react_native_linux
