#pragma once

#include "NotificationCenter.h"

#include <array>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <string_view>

namespace facebook::react {

class CallInvoker;
class TurboModule;

} // namespace facebook::react

namespace react_native_linux {

class NotificationEvent;

/**
 * What expo-notifications' native modules share (#274, ADR-0004): `NotificationCenter` and the events it emits, one
 * per upstream event name, each subscribed to through the module that upstream subscribes it on.
 *
 * Threading contract: `TurboModuleRegistry` owns it and destroys it after the JavaScript thread has quit; the center
 * is destroyed first, so its worker never emits into a destroyed event. The center's own contract covers the rest.
 */
class ExpoNotifications final {
public:
    ExpoNotifications();
    ExpoNotifications(const ExpoNotifications&) = delete;
    ExpoNotifications(ExpoNotifications&&) = delete;
    ExpoNotifications& operator=(const ExpoNotifications&) = delete;
    ExpoNotifications& operator=(ExpoNotifications&&) = delete;
    ~ExpoNotifications();

    [[nodiscard]] NotificationCenter& center() noexcept;
    [[nodiscard]] std::shared_ptr<NotificationEvent> event(std::string_view name) const;

private:
    std::map<std::string, std::shared_ptr<NotificationEvent>, std::less<>> events_;
    NotificationCenter center_;
};

/**
 * Every native module upstream's `expo-notifications` index requires as it is imported on a native platform; an
 * unregistered one would make `import 'expo-notifications'` throw. Each answers only what this platform does:
 *
 * - `ExpoNotificationsEmitter`, `ExpoNotificationsHandlerModule`, `ExpoNotificationScheduler`,
 *   `ExpoNotificationPresenter` (`dismissAllNotificationsAsync` only) and `ExpoNotificationPermissionsModule`, over
 *   `NotificationCenter`.
 * - `ExpoPushTokenManager` has the `onDevicePushToken` event, which never fires, and no `getDevicePushTokenAsync`: a
 *   desktop session has no push service, so upstream rejects the token with its own `UnavailabilityError`
 *   (`ERR_UNAVAILABLE`), as it does on a platform without one.
 * - `ExpoBadgeModule`, `ExpoBackgroundNotificationTasksModule`, `ExpoNotificationCategoriesModule` and
 *   `NotificationsServerRegistrationModule` answer nothing, so each of their calls throws upstream's
 *   `UnavailabilityError`.
 */
inline constexpr std::array<std::string_view, 10> kExpoNotificationsModuleNames{
    "ExpoBackgroundNotificationTasksModule",
    "ExpoBadgeModule",
    "ExpoNotificationCategoriesModule",
    "ExpoNotificationPermissionsModule",
    "ExpoNotificationPresenter",
    "ExpoNotificationScheduler",
    "ExpoNotificationsEmitter",
    "ExpoNotificationsHandlerModule",
    "ExpoPushTokenManager",
    "NotificationsServerRegistrationModule",
};

/**
 * The module of `name`, one of `kExpoNotificationsModuleNames`, over `notifications`, which must outlive it.
 *
 * Threading contract: every method runs on the JavaScript thread and hands its work to the center; a promise settles
 * from the center's worker, through the call invoker.
 */
std::shared_ptr<facebook::react::TurboModule>
makeExpoNotificationsModule(std::string_view name, std::shared_ptr<facebook::react::CallInvoker> jsInvoker,
                            ExpoNotifications& notifications);

} // namespace react_native_linux
