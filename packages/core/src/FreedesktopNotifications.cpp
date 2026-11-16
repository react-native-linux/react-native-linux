#include "FreedesktopNotifications.h"

#include <algorithm>
#include <cstdint>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <system_error>
#include <utility>

namespace react_native_linux {

namespace {

constexpr char kDestination[] = "org.freedesktop.Notifications";
constexpr char kPath[] = "/org/freedesktop/Notifications";
constexpr char kInterface[] = "org.freedesktop.Notifications";
constexpr int kActionArrayLength = 2;
constexpr int kNoHints = 0;
constexpr int32_t kServerDecidesExpiry = -1;

struct MessageUnref {
    void operator()(sd_bus_message* message) const noexcept { sd_bus_message_unref(message); }
};

using Message = std::unique_ptr<sd_bus_message, MessageUnref>;

std::string senderOf(sd_bus_message* message) {
    const char* sender = sd_bus_message_get_sender(message);

    return sender == nullptr ? std::string() : std::string(sender);
}

/** Throws naming `member` and the D-Bus error, which `sd_bus_error_set_errno` keeps when the call already set one. */
void require(int result, const char* member, SdBusError& error) {
    if (result < 0) {
        sd_bus_error_set_errno(&error.value, result);

        throw std::runtime_error(std::string(kInterface) + "." + member + ": " + error.value.name);
    }
}

} // namespace

void FreedesktopNotifications::BusClose::operator()(sd_bus* bus) const noexcept { sd_bus_close_unref(bus); }

void FreedesktopNotifications::SlotUnref::operator()(sd_bus_slot* slot) const noexcept { sd_bus_slot_unref(slot); }

FreedesktopNotifications::FreedesktopNotifications(sd_bus* bus, std::string applicationName, Signals signals)
    : bus_(bus), applicationName_(std::move(applicationName)), signals_(std::move(signals)) {
    sd_bus_slot* actionInvoked = nullptr;
    sd_bus_slot* closed = nullptr;

    sd_bus_set_method_call_timeout(bus, kCallTimeout.count());
    sd_bus_match_signal(bus, &actionInvoked, nullptr, kPath, kInterface, "ActionInvoked", &onActionInvoked, this);
    sd_bus_match_signal(bus, &closed, nullptr, kPath, kInterface, "NotificationClosed", &onClosed, this);
    actionInvokedSlot_.reset(actionInvoked);
    closedSlot_.reset(closed);
}

std::unique_ptr<NotificationServer> FreedesktopNotifications::connect(const BusOpener& openBus,
                                                                      std::string applicationName, Signals signals) {
    sd_bus* bus = nullptr;
    const int opened = openBus(&bus);

    if (opened < 0) {
        sd_bus_close_unref(bus);
        std::cerr << "[rnl-notifications] no session bus: "
                  << std::error_code(-opened, std::generic_category()).message() << '\n';

        return nullptr;
    }

    return std::unique_ptr<NotificationServer>(
        new FreedesktopNotifications(bus, std::move(applicationName), std::move(signals)));
}

bool FreedesktopNotifications::reachable() {
    SdBusError error;
    sd_bus_message* reply = nullptr;
    const int result = sd_bus_call_method(bus_.get(), kDestination, kPath, kInterface, "GetServerInformation",
                                          &error.value, &reply, "");

    sd_bus_message_unref(reply);

    return result >= 0;
}

uint32_t FreedesktopNotifications::notify(const std::string& summary, const std::string& body) {
    SdBusError error;
    sd_bus_message* reply = nullptr;
    const int result =
        sd_bus_call_method(bus_.get(), kDestination, kPath, kInterface, "Notify", &error.value, &reply, "susssasa{sv}i",
                           applicationName_.c_str(), 0U, "", summary.c_str(), body.c_str(), kActionArrayLength,
                           "default", "Open", kNoHints, kServerDecidesExpiry);
    const Message owned(reply);
    uint32_t serverId = 0;

    require(result, "Notify", error);
    require(sd_bus_message_read(reply, "u", &serverId), "Notify", error);
    daemonName_ = senderOf(reply);

    return serverId;
}

void FreedesktopNotifications::close(uint32_t serverId) {
    SdBusError error;
    sd_bus_message* reply = nullptr;
    const int result = sd_bus_call_method(bus_.get(), kDestination, kPath, kInterface, "CloseNotification",
                                          &error.value, &reply, "u", serverId);

    sd_bus_message_unref(reply);
    require(result, "CloseNotification", error);
}

pollfd FreedesktopNotifications::pollDescriptor() {
    return pollfd{.fd = sd_bus_get_fd(bus_.get()),
                  .events = static_cast<short>(std::max(sd_bus_get_events(bus_.get()), 0)),
                  .revents = 0};
}

void FreedesktopNotifications::process() {
    while (sd_bus_process(bus_.get(), nullptr) > 0) {
    }
}

bool FreedesktopNotifications::fromDaemon(sd_bus_message* message) const { return senderOf(message) == daemonName_; }

int FreedesktopNotifications::onActionInvoked(sd_bus_message* message, void* userData, sd_bus_error* /*error*/) {
    auto* const self = static_cast<FreedesktopNotifications*>(userData);
    uint32_t serverId = 0;
    const char* actionKey = nullptr;

    if (self->fromDaemon(message) && sd_bus_message_read(message, "us", &serverId, &actionKey) >= 0) {
        self->signals_.actionInvoked(serverId, actionKey);
    }

    return 0;
}

int FreedesktopNotifications::onClosed(sd_bus_message* message, void* userData, sd_bus_error* /*error*/) {
    auto* const self = static_cast<FreedesktopNotifications*>(userData);
    uint32_t serverId = 0;
    uint32_t reason = 0;

    if (self->fromDaemon(message) && sd_bus_message_read(message, "uu", &serverId, &reason) >= 0) {
        self->signals_.closed(serverId);
    }

    return 0;
}

std::unique_ptr<NotificationServer> connectSessionNotifications(const std::string& applicationName,
                                                                NotificationServer::Signals signals) {
    return FreedesktopNotifications::connect(sd_bus_open_user, applicationName, std::move(signals));
}

} // namespace react_native_linux
