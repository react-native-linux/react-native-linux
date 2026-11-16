#include "FakeNotificationDaemon.h"

#include <cstdint>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace react_native_linux {

namespace {

constexpr char kPath[] = "/org/freedesktop/Notifications";
constexpr char kInterface[] = "org.freedesktop.Notifications";
constexpr uint32_t kDismissedByUser = 2;
constexpr uint32_t kClosedByCall = 3;

std::vector<std::string> readActions(sd_bus_message* message) {
    std::vector<std::string> actions;
    const char* action = nullptr;

    sd_bus_message_enter_container(message, 'a', "s");

    while (sd_bus_message_read(message, "s", &action) > 0) {
        actions.emplace_back(action);
    }

    sd_bus_message_exit_container(message);

    return actions;
}

} // namespace

FakeNotificationDaemon::FakeNotificationDaemon(sd_bus* bus, Behavior behavior) : behavior_(std::move(behavior)) {
    sd_bus_slot* slot = nullptr;

    sd_bus_add_filter(bus, &slot, &FakeNotificationDaemon::onMessage, this);
    slot_.reset(slot);
}

std::vector<FakeNotificationDaemon::Notification> FakeNotificationDaemon::notifications() const {
    const std::lock_guard lock(mutex_);

    return notifications_;
}

std::vector<uint32_t> FakeNotificationDaemon::closed() const {
    const std::lock_guard lock(mutex_);

    return closed_;
}

int FakeNotificationDaemon::onMessage(sd_bus_message* message, void* userData, sd_bus_error* /*error*/) {
    if (sd_bus_message_is_method_call(message, kInterface, nullptr) <= 0) {
        return 0;
    }

    return static_cast<FakeNotificationDaemon*>(userData)->answer(message);
}

int FakeNotificationDaemon::answer(sd_bus_message* message) {
    sd_bus* const bus = sd_bus_message_get_bus(message);
    const std::string member = sd_bus_message_get_member(message);

    if (behavior_.failsCalls) {
        return sd_bus_reply_method_errorf(message, SD_BUS_ERROR_FAILED, "%s failed", member.c_str());
    }

    if (behavior_.answersMalformed) {
        return sd_bus_reply_method_return(message, "s", "malformed");
    }

    if (member == "GetServerInformation") {
        return sd_bus_reply_method_return(message, "ssss", "rnl-fake-notifications", "react-native-linux", "1", "1.2");
    }

    if (member == "CloseNotification") {
        uint32_t identifier = 0;

        sd_bus_message_read(message, "u", &identifier);
        {
            const std::lock_guard lock(mutex_);

            closed_.push_back(identifier);
        }
        sd_bus_reply_method_return(message, "");
        emitClosed(bus, identifier, kClosedByCall);

        return 1;
    }

    Notification notification;
    const char* applicationName = nullptr;
    uint32_t replacesIdentifier = 0;
    const char* icon = nullptr;
    const char* summary = nullptr;
    const char* body = nullptr;

    sd_bus_message_read(message, "susss", &applicationName, &replacesIdentifier, &icon, &summary, &body);
    notification.applicationName = applicationName;
    notification.summary = summary;
    notification.body = body;
    notification.actions = readActions(message);
    sd_bus_message_skip(message, "a{sv}");
    sd_bus_message_read(message, "i", &notification.expireTimeout);

    uint32_t identifier = 0;

    {
        const std::lock_guard lock(mutex_);

        notifications_.push_back(notification);
        identifier = static_cast<uint32_t>(notifications_.size());
    }

    log("Notify app=" + notification.applicationName + " summary=" + notification.summary +
        " body=" + notification.body + " id=" + std::to_string(identifier));
    sd_bus_reply_method_return(message, "u", identifier);

    if (!behavior_.invokedAction.empty()) {
        if (behavior_.emitsMalformedSignals) {
            sd_bus_emit_signal(bus, kPath, kInterface, "ActionInvoked", "s", "malformed");
        }

        sd_bus_emit_signal(bus, kPath, kInterface, "ActionInvoked", "us", identifier, behavior_.invokedAction.c_str());
        log("ActionInvoked id=" + std::to_string(identifier) + " action=" + behavior_.invokedAction);
        emitClosed(bus, identifier, kDismissedByUser);
    }

    return 1;
}

void FakeNotificationDaemon::emitClosed(sd_bus* bus, uint32_t identifier, uint32_t reason) const {
    if (behavior_.emitsMalformedSignals) {
        sd_bus_emit_signal(bus, kPath, kInterface, "NotificationClosed", "s", "malformed");
    }

    sd_bus_emit_signal(bus, kPath, kInterface, "NotificationClosed", "uu", identifier, reason);
}

void FakeNotificationDaemon::log(const std::string& line) const {
    if (behavior_.log) {
        behavior_.log(line);
    }
}

} // namespace react_native_linux
