#pragma once

#include "NotificationCenter.h"

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <systemd/sd-bus.h>

namespace react_native_linux {

/**
 * `org.freedesktop.Notifications` (the Desktop Notifications Specification, which GNOME Shell, KDE Plasma, dunst and
 * mako serve) over sd-bus, the client `AppearancePortal` already links. A notification is offered with the `default`
 * action, which a click on it invokes. Every call gives up after `kCallTimeout`, so a hung daemon costs the worker
 * that much and never the JavaScript thread.
 *
 * Threading contract: not thread-safe. `NotificationCenter` connects it and calls it only from its worker thread.
 */
class FreedesktopNotifications final : public NotificationServer {
public:
    /** Fills its argument with a connected bus and answers its result, as `sd_bus_open_user` does. */
    using BusOpener = std::function<int(sd_bus**)>;

    static constexpr std::chrono::microseconds kCallTimeout = std::chrono::seconds(5);

    /** Null, with the reason on standard error, when `openBus` fails. */
    static std::unique_ptr<NotificationServer> connect(const BusOpener& openBus, std::string applicationName,
                                                       Signals signals);

    [[nodiscard]] bool reachable() override;
    uint32_t notify(const std::string& summary, const std::string& body) override;
    void close(uint32_t serverId) override;
    [[nodiscard]] pollfd pollDescriptor() override;
    void process() override;

private:
    struct BusClose {
        void operator()(sd_bus* bus) const noexcept;
    };

    struct SlotUnref {
        void operator()(sd_bus_slot* slot) const noexcept;
    };

    FreedesktopNotifications(sd_bus* bus, std::string applicationName, Signals signals);

    static int onActionInvoked(sd_bus_message* message, void* userData, sd_bus_error* error);
    static int onClosed(sd_bus_message* message, void* userData, sd_bus_error* error);

    std::unique_ptr<sd_bus, BusClose> bus_;
    std::string applicationName_;
    Signals signals_;
    std::unique_ptr<sd_bus_slot, SlotUnref> actionInvokedSlot_;
    std::unique_ptr<sd_bus_slot, SlotUnref> closedSlot_;
};

/** `NotificationCenter`'s connector for this session: the daemon on the bus `sd_bus_open_user` opens. */
std::unique_ptr<NotificationServer> connectSessionNotifications(const std::string& applicationName,
                                                                NotificationServer::Signals signals);

} // namespace react_native_linux
