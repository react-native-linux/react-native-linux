#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <systemd/sd-bus.h>
#include <vector>

namespace react_native_linux {

/**
 * An `org.freedesktop.Notifications` daemon that records what it is asked, for `FreedesktopNotificationsTest` (on a
 * socket pair) and the `expo-notifications` e2e (`rnl_fake_notifications`, on a private session bus). Every reaction
 * happens on the thread that processes its bus, inside the call that causes it, so it needs no thread of its own.
 *
 * Threading contract: the bus is processed on one thread; `notifications` and `closed` may be read from any other.
 */
class FakeNotificationDaemon final {
public:
    struct Notification {
        std::string applicationName;
        std::string summary;
        std::string body;
        std::vector<std::string> actions;
        int32_t expireTimeout{0};
    };

    struct Behavior {
        /** When set, every notification is clicked as soon as it shows: `ActionInvoked` with this key, then closed. */
        std::string invokedAction;
        bool failsCalls{false};
        bool answersMalformed{false};
        /** Signals whose arguments do not match the specification, before each well-formed one. */
        bool emitsMalformedSignals{false};
        /**
         * On a socket pair, where no broker stamps one: the sender its `Notify` replies and signals carry, and the
         * sender of a copy of each signal that another client forges just before it.
         */
        std::string sender;
        std::string forgedSender;
        std::function<void(const std::string& line)> log;
    };

    FakeNotificationDaemon(sd_bus* bus, Behavior behavior);
    FakeNotificationDaemon(const FakeNotificationDaemon&) = delete;
    FakeNotificationDaemon(FakeNotificationDaemon&&) = delete;
    FakeNotificationDaemon& operator=(const FakeNotificationDaemon&) = delete;
    FakeNotificationDaemon& operator=(FakeNotificationDaemon&&) = delete;
    ~FakeNotificationDaemon() = default;

    [[nodiscard]] std::vector<Notification> notifications() const;
    [[nodiscard]] std::vector<uint32_t> closed() const;

private:
    struct SlotUnref {
        void operator()(sd_bus_slot* slot) const noexcept { sd_bus_slot_unref(slot); }
    };

    static int onMessage(sd_bus_message* message, void* userData, sd_bus_error* error);
    int answer(sd_bus_message* message);
    void emitClosed(sd_bus* bus, uint32_t identifier, uint32_t reason) const;
    void emitActionInvoked(sd_bus* bus, uint32_t identifier) const;
    [[nodiscard]] std::vector<std::string> signalSenders() const;
    void log(const std::string& line) const;

    Behavior behavior_;
    std::unique_ptr<sd_bus_slot, SlotUnref> slot_;
    mutable std::mutex mutex_;
    std::vector<Notification> notifications_;
    std::vector<uint32_t> closed_;
};

} // namespace react_native_linux
