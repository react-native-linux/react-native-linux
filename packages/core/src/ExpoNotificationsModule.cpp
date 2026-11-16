#include "ExpoNotificationsModule.h"

#include <ReactCommon/CallInvoker.h>
#include <ReactCommon/TurboModule.h>
#include <cstddef>
#include <functional>
#include <initializer_list>
#include <jsi/JSIDynamic.h>
#include <jsi/jsi.h>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>

#include <react/bridging/Bridging.h>
#include <react/bridging/Dynamic.h>
#include <react/bridging/EventEmitter.h>
#include <react/bridging/Promise.h>

namespace react_native_linux {

/**
 * React Native's `AsyncEventEmitter` with a payload, plus whether anything listens, which `NotificationCenter` asks
 * before it offers a notification to the handler.
 *
 * Threading contract: `emit` and `hasListeners` may be called from any thread; a listener runs on the JavaScript
 * thread, through the call invoker its subscription captured.
 */
class NotificationEvent final : public facebook::react::IAsyncEventEmitter {
public:
    void emit(folly::dynamic payload) {
        const std::lock_guard lock(state_->mutex);

        for (const auto& [identifier, listener] : state_->listeners) {
            listener.call(payload);
        }
    }

    [[nodiscard]] bool hasListeners() const {
        const std::lock_guard lock(state_->mutex);

        return !state_->listeners.empty();
    }

    facebook::jsi::Object get(facebook::jsi::Runtime& runtime,
                              const std::shared_ptr<facebook::react::CallInvoker>& jsInvoker) const override {
        return facebook::react::bridging::toJs(runtime, listen_, jsInvoker);
    }

private:
    using Listener = facebook::react::AsyncCallback<folly::dynamic>;

    struct State {
        std::mutex mutex;
        std::unordered_map<size_t, Listener> listeners;
        size_t nextIdentifier{0};
    };

    std::shared_ptr<State> state_{std::make_shared<State>()};
    std::function<facebook::react::EventSubscription(Listener)> listen_{[state = state_](Listener listener) {
        const std::lock_guard lock(state->mutex);
        const size_t identifier = state->nextIdentifier++;

        state->listeners.emplace(identifier, std::move(listener));

        return facebook::react::EventSubscription([state, identifier] {
            const std::lock_guard innerLock(state->mutex);

            state->listeners.erase(identifier);
        });
    }};
};

namespace {

constexpr std::array<std::string_view, 7> kEventNames{
    "onDevicePushToken",        "onDidClearNotificationResponse",
    "onDidReceiveNotification", "onDidReceiveNotificationResponse",
    "onHandleNotification",     "onHandleNotificationTimeout",
    "onNotificationsDeleted",
};

std::map<std::string, std::shared_ptr<NotificationEvent>, std::less<>> makeEvents() {
    std::map<std::string, std::shared_ptr<NotificationEvent>, std::less<>> events;

    for (const std::string_view name : kEventNames) {
        events.emplace(name, std::make_shared<NotificationEvent>());
    }

    return events;
}

void requireArguments(facebook::jsi::Runtime& runtime, size_t count, size_t required, const char* message) {
    if (count < required) {
        throw facebook::jsi::JSError(runtime, message);
    }
}

/** Whether a notification behavior, as upstream's handler answers it, shows the notification. */
bool shows(facebook::jsi::Runtime& runtime, const facebook::jsi::Value& behavior) {
    if (!behavior.isObject()) {
        return false;
    }

    const facebook::jsi::Object object = behavior.getObject(runtime);

    for (const char* key : {"shouldShowBanner", "shouldShowList", "shouldShowAlert"}) {
        const facebook::jsi::Value value = object.getProperty(runtime, key);

        if (value.isBool() && value.getBool()) {
            return true;
        }
    }

    return false;
}

class ExpoNotificationsModule final : public facebook::react::TurboModule {
public:
    ExpoNotificationsModule(std::string_view name, std::shared_ptr<facebook::react::CallInvoker> jsInvoker,
                            ExpoNotifications& notifications)
        : TurboModule(std::string(name), std::move(jsInvoker)), notifications_(notifications) {
        if (name == "ExpoNotificationsEmitter") {
            methodMap_["getLastNotificationResponse"] = {0, &getLastNotificationResponse};
            methodMap_["clearLastNotificationResponse"] = {0, &clearLastNotificationResponse};
            methodMap_["emit"] = {1, &emit};
            listen({"onDidReceiveNotification", "onDidReceiveNotificationResponse", "onDidClearNotificationResponse",
                    "onNotificationsDeleted"});
        } else if (name == "ExpoNotificationsHandlerModule") {
            methodMap_["handleNotificationAsync"] = {2, &handleNotificationAsync};
            listen({"onHandleNotification", "onHandleNotificationTimeout"});
        } else if (name == "ExpoNotificationPermissionsModule") {
            methodMap_["getPermissionsAsync"] = {0, &getPermissionsAsync};
            methodMap_["requestPermissionsAsync"] = {1, &getPermissionsAsync};
        } else if (name == "ExpoNotificationScheduler") {
            methodMap_["scheduleNotificationAsync"] = {3, &scheduleNotificationAsync};
        } else if (name == "ExpoNotificationPresenter") {
            methodMap_["dismissAllNotificationsAsync"] = {0, &dismissAllNotificationsAsync};
        } else if (name == "ExpoPushTokenManager") {
            listen({"onDevicePushToken"});
        }
    }

private:
    using Start = std::function<void(NotificationCenter&, NotificationCenter::Resolve, NotificationCenter::Reject)>;

    void listen(std::initializer_list<std::string_view> events) {
        for (const std::string_view event : events) {
            eventEmitterMap_[std::string(event)] = notifications_.event(event);
        }
    }

    static facebook::jsi::Value settle(facebook::jsi::Runtime& runtime, TurboModule& turboModule, const Start& start) {
        auto& module = static_cast<ExpoNotificationsModule&>(turboModule);
        facebook::react::AsyncPromise<folly::dynamic> promise(runtime, module.jsInvoker_);
        facebook::jsi::Value value = promise.get(runtime);

        start(
            module.notifications_.center(),
            [promise](folly::dynamic result) mutable { promise.resolve(std::move(result)); },
            [promise](const std::string& message) mutable { promise.reject(facebook::react::Error(message)); });

        return value;
    }

    static facebook::jsi::Value getLastNotificationResponse(facebook::jsi::Runtime& runtime, TurboModule& turboModule,
                                                            const facebook::jsi::Value* /*arguments*/,
                                                            size_t /*count*/) {
        return facebook::jsi::valueFromDynamic(
            runtime, static_cast<ExpoNotificationsModule&>(turboModule).notifications_.center().lastResponse());
    }

    static facebook::jsi::Value clearLastNotificationResponse(facebook::jsi::Runtime& /*runtime*/,
                                                              TurboModule& turboModule,
                                                              const facebook::jsi::Value* /*arguments*/,
                                                              size_t /*count*/) {
        static_cast<ExpoNotificationsModule&>(turboModule).notifications_.center().clearLastResponse();

        return facebook::jsi::Value::undefined();
    }

    /** What upstream's JavaScript `EventEmitter.emit` does on these modules: `clearLastNotificationResponse` calls it.
     */
    static facebook::jsi::Value emit(facebook::jsi::Runtime& runtime, TurboModule& turboModule,
                                     const facebook::jsi::Value* arguments, size_t count) {
        requireArguments(runtime, count, 1, "ExpoNotificationsEmitter.emit takes an event name");

        const std::string name = arguments[0].asString(runtime).utf8(runtime);
        const std::shared_ptr<NotificationEvent> event =
            static_cast<ExpoNotificationsModule&>(turboModule).notifications_.event(name);

        if (event == nullptr) {
            throw facebook::jsi::JSError(runtime, "ExpoNotificationsEmitter has no event " + name);
        }

        event->emit(count > 1 ? facebook::jsi::dynamicFromValue(runtime, arguments[1]) : folly::dynamic(nullptr));

        return facebook::jsi::Value::undefined();
    }

    static facebook::jsi::Value handleNotificationAsync(facebook::jsi::Runtime& runtime, TurboModule& turboModule,
                                                        const facebook::jsi::Value* arguments, size_t count) {
        requireArguments(runtime, count, 2,
                         "ExpoNotificationsHandlerModule.handleNotificationAsync takes an identifier and a behavior");

        std::string identifier = arguments[0].asString(runtime).utf8(runtime);
        const bool show = shows(runtime, arguments[1]);

        return settle(runtime, turboModule, [&identifier, show](NotificationCenter& center, auto resolve, auto reject) {
            center.handle(std::move(identifier), show, std::move(resolve), std::move(reject));
        });
    }

    static facebook::jsi::Value getPermissionsAsync(facebook::jsi::Runtime& runtime, TurboModule& turboModule,
                                                    const facebook::jsi::Value* /*arguments*/, size_t /*count*/) {
        return settle(runtime, turboModule, [](NotificationCenter& center, auto resolve, auto /*reject*/) {
            center.getPermissions(std::move(resolve));
        });
    }

    static facebook::jsi::Value scheduleNotificationAsync(facebook::jsi::Runtime& runtime, TurboModule& turboModule,
                                                          const facebook::jsi::Value* arguments, size_t count) {
        requireArguments(
            runtime, count, 3,
            "ExpoNotificationScheduler.scheduleNotificationAsync takes an identifier, a content and a trigger");

        std::string identifier = arguments[0].asString(runtime).utf8(runtime);
        folly::dynamic content = facebook::jsi::dynamicFromValue(runtime, arguments[1]);
        folly::dynamic trigger = facebook::jsi::dynamicFromValue(runtime, arguments[2]);

        return settle(runtime, turboModule, [&](NotificationCenter& center, auto resolve, auto reject) {
            center.schedule(std::move(identifier), std::move(content), std::move(trigger), std::move(resolve),
                            std::move(reject));
        });
    }

    static facebook::jsi::Value dismissAllNotificationsAsync(facebook::jsi::Runtime& runtime, TurboModule& turboModule,
                                                             const facebook::jsi::Value* /*arguments*/,
                                                             size_t /*count*/) {
        return settle(runtime, turboModule, [](NotificationCenter& center, auto resolve, auto /*reject*/) {
            center.dismissAll(std::move(resolve));
        });
    }

    ExpoNotifications& notifications_;
};

} // namespace

ExpoNotifications::ExpoNotifications()
    : events_(makeEvents()),
      center_([this](const std::string& name, folly::dynamic payload) { events_.at(name)->emit(std::move(payload)); },
              [this](const std::string& name) { return events_.at(name)->hasListeners(); }) {}

ExpoNotifications::~ExpoNotifications() = default;

NotificationCenter& ExpoNotifications::center() noexcept { return center_; }

std::shared_ptr<NotificationEvent> ExpoNotifications::event(std::string_view name) const {
    const auto found = events_.find(name);

    return found == events_.end() ? nullptr : found->second;
}

std::shared_ptr<facebook::react::TurboModule>
makeExpoNotificationsModule(std::string_view name, std::shared_ptr<facebook::react::CallInvoker> jsInvoker,
                            ExpoNotifications& notifications) {
    return std::make_shared<ExpoNotificationsModule>(name, std::move(jsInvoker), notifications);
}

} // namespace react_native_linux
