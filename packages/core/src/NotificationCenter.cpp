#include "NotificationCenter.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <climits>
#include <cmath>
#include <cstdint>
#include <exception>
#include <folly/json.h>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <sys/eventfd.h>
#include <unistd.h>
#include <utility>
#include <vector>

namespace react_native_linux {

namespace {

constexpr char kDefaultActionIdentifier[] = "expo.modules.notifications.actions.DEFAULT";
constexpr double kMillisecondsPerSecond = 1000;

double epochMilliseconds() {
    return static_cast<double>(
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch())
            .count());
}

/** How long until `trigger`, as upstream's `parseTrigger` hands it over, fires; throws for one Linux cannot fire. */
std::chrono::milliseconds delayUntil(const folly::dynamic& trigger) {
    if (trigger.isNull()) {
        return std::chrono::milliseconds(0);
    }

    const folly::dynamic type = trigger.getDefault("type");
    double milliseconds = 0;

    if (type == "timeInterval" && trigger.getDefault("repeats", false) == false) {
        milliseconds = trigger.at("seconds").asDouble() * kMillisecondsPerSecond;
    } else if (type == "date") {
        milliseconds = trigger.at("timestamp").asDouble() - epochMilliseconds();
    } else {
        folly::json::serialization_opts sorted;

        sorted.sort_keys = true;

        throw std::invalid_argument("Trigger " + folly::json::serialize(trigger, sorted) +
                                    " is not supported on Linux, which fires null, a non-repeating timeInterval and a "
                                    "date trigger.");
    }

    return std::chrono::milliseconds(std::llround(std::max(milliseconds, 0.0)));
}

std::string text(const folly::dynamic& content, const char* key) {
    const folly::dynamic value = content.getDefault(key);

    return value.isString() ? value.asString() : std::string();
}

folly::dynamic permission(bool granted) {
    return folly::dynamic::object("status", granted ? "granted" : "undetermined")("granted", granted)(
        "canAskAgain", true)("expires", "never");
}

} // namespace

NotificationCenter::NotificationCenter(Emit emit, HasListeners hasListeners, std::chrono::milliseconds handlerTimeout)
    : emit_(std::move(emit)), hasListeners_(std::move(hasListeners)), handlerTimeout_(handlerTimeout),
      wakeDescriptor_(::eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK)) {}

NotificationCenter::~NotificationCenter() {
    {
        const std::lock_guard lock(mutex_);

        stopping_ = true;
    }

    const uint64_t one = 1;

    ::write(wakeDescriptor_, &one, sizeof one);

    if (thread_.joinable()) {
        thread_.join();
    }

    ::close(wakeDescriptor_);
}

void NotificationCenter::configure(std::string applicationName, NotificationServerConnector connect) {
    post([this, applicationName = std::move(applicationName), connect = std::move(connect)]() mutable {
        applicationName_ = std::move(applicationName);
        connect_ = std::move(connect);
    });
}

void NotificationCenter::getPermissions(Resolve resolve) {
    post([this, resolve = std::move(resolve)] {
        NotificationServer* const daemon = server();

        resolve(permission(daemon != nullptr && daemon->reachable()));
    });
}

void NotificationCenter::schedule(std::string identifier, folly::dynamic content, folly::dynamic trigger,
                                  Resolve resolve, Reject reject) {
    post([this, identifier = std::move(identifier), content = std::move(content), trigger = std::move(trigger),
          resolve = std::move(resolve), reject = std::move(reject)]() mutable {
        try {
            if (!content.isObject()) {
                throw std::invalid_argument("A notification's content must be an object.");
            }

            const std::chrono::milliseconds delay = delayUntil(trigger);

            scheduled_[identifier] = Pending{
                .deadline = Clock::now() + delay,
                .notification = folly::dynamic::object(
                    "request", folly::dynamic::object("identifier", identifier)("content", std::move(content))(
                                   "trigger", std::move(trigger)))("date", nullptr),
            };
        } catch (const std::exception& error) {
            reject(error.what());

            return;
        }

        resolve(identifier);
    });
}

void NotificationCenter::handle(std::string identifier, bool shows, Resolve resolve, Reject reject) {
    post([this, identifier = std::move(identifier), shows, resolve = std::move(resolve), reject = std::move(reject)] {
        const auto found = handling_.find(identifier);

        if (found == handling_.end()) {
            reject("Failed to handle notification " + identifier + ", it has already been handled.");

            return;
        }

        const folly::dynamic notification = std::move(found->second.notification);

        handling_.erase(found);

        if (shows) {
            present(notification);
        }

        resolve(nullptr);
    });
}

void NotificationCenter::dismissAll(Resolve resolve) {
    post([this, resolve = std::move(resolve)] {
        for (const auto& [serverId, notification] : presented_) {
            try {
                server_->close(serverId);
            } catch (const std::exception& error) {
                std::cerr << "[rnl-notifications] " << error.what() << '\n';
            }
        }

        presented_.clear();
        resolve(nullptr);
    });
}

folly::dynamic NotificationCenter::lastResponse() const {
    const std::lock_guard lock(responseMutex_);

    return lastResponse_;
}

void NotificationCenter::clearLastResponse() {
    const std::lock_guard lock(responseMutex_);

    lastResponse_ = nullptr;
}

void NotificationCenter::post(Task task) {
    {
        const std::lock_guard lock(mutex_);

        tasks_.push_back(std::move(task));

        if (!thread_.joinable()) {
            thread_ = std::thread(&NotificationCenter::run, this);
        }
    }

    const uint64_t one = 1;

    ::write(wakeDescriptor_, &one, sizeof one);
}

void NotificationCenter::run() {
    while (true) {
        std::deque<Task> tasks;

        {
            const std::lock_guard lock(mutex_);

            if (stopping_) {
                return;
            }

            tasks.swap(tasks_);
        }

        for (const Task& task : tasks) {
            task();
        }

        fireDueTimers();

        if (server_ != nullptr) {
            server_->process();
        }

        waitForWork();
    }
}

void NotificationCenter::waitForWork() {
    std::optional<Clock::time_point> next;

    for (const auto* pending : {&scheduled_, &handling_}) {
        for (const auto& [identifier, entry] : *pending) {
            next = std::min(next.value_or(entry.deadline), entry.deadline);
        }
    }

    const int64_t timeout =
        next.has_value() ? std::chrono::ceil<std::chrono::milliseconds>(next.value() - Clock::now()).count() : -1;
    std::array<pollfd, 2> descriptors{
        pollfd{.fd = wakeDescriptor_, .events = POLLIN, .revents = 0},
        server_ != nullptr ? server_->pollDescriptor() : pollfd{.fd = -1, .events = 0, .revents = 0},
    };
    uint64_t wakes = 0;

    ::poll(descriptors.data(), descriptors.size(), static_cast<int>(std::clamp<int64_t>(timeout, -1, INT_MAX)));
    ::read(wakeDescriptor_, &wakes, sizeof wakes);
}

void NotificationCenter::fireDueTimers() {
    const Clock::time_point now = Clock::now();
    const auto takeDue = [now](std::map<std::string, Pending>& pending) {
        std::vector<folly::dynamic> due;

        for (auto entry = pending.begin(); entry != pending.end();) {
            if (entry->second.deadline > now) {
                ++entry;
                continue;
            }

            due.push_back(std::move(entry->second.notification));
            entry = pending.erase(entry);
        }

        return due;
    };

    for (folly::dynamic& notification : takeDue(handling_)) {
        emit_("onHandleNotificationTimeout",
              folly::dynamic::object("id", notification["request"]["identifier"])("notification", notification));
        present(notification);
    }

    for (const folly::dynamic& notification : takeDue(scheduled_)) {
        deliver(notification);
    }
}

void NotificationCenter::deliver(const folly::dynamic& scheduled) {
    folly::dynamic notification = scheduled;

    notification["date"] = epochMilliseconds();
    emit_("onDidReceiveNotification", notification);

    if (!hasListeners_("onHandleNotification")) {
        return;
    }

    const std::string identifier = notification["request"]["identifier"].asString();

    handling_[identifier] = Pending{.deadline = Clock::now() + handlerTimeout_, .notification = notification};
    emit_("onHandleNotification", folly::dynamic::object("id", identifier)("notification", std::move(notification)));
}

void NotificationCenter::present(const folly::dynamic& notification) {
    NotificationServer* const daemon = server();
    const folly::dynamic& content = notification["request"]["content"];

    if (daemon == nullptr) {
        std::cerr << "[rnl-notifications] no session bus: " << notification["request"]["identifier"].asString()
                  << " is not shown\n";

        return;
    }

    try {
        presented_[daemon->notify(text(content, "title"), text(content, "body"))] = notification;
    } catch (const std::exception& error) {
        std::cerr << "[rnl-notifications] " << error.what() << '\n';
    }
}

NotificationServer* NotificationCenter::server() {
    if (server_ == nullptr && connect_) {
        server_ = connect_(applicationName_, NotificationServer::Signals{
                                                 .actionInvoked =
                                                     [this](uint32_t serverId, const std::string& actionKey) {
                                                         onActionInvoked(serverId, actionKey);
                                                     },
                                                 .closed = [this](uint32_t serverId) { presented_.erase(serverId); },
                                             });
    }

    return server_.get();
}

void NotificationCenter::onActionInvoked(uint32_t serverId, const std::string& actionKey) {
    const auto found = presented_.find(serverId);

    if (found == presented_.end()) {
        return;
    }

    folly::dynamic response = folly::dynamic::object("notification", found->second)(
        "actionIdentifier", actionKey == "default" ? kDefaultActionIdentifier : actionKey);

    {
        const std::lock_guard lock(responseMutex_);

        lastResponse_ = response;
    }

    emit_("onDidReceiveNotificationResponse", std::move(response));
}

} // namespace react_native_linux
