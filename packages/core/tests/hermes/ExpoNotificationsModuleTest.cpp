#include "NotificationCenter.h"
#include "OnJavaScriptThread.h"
#include "ReactHost.h"

#include <cstdint>
#include <cxxreact/JSBigString.h>
#include <folly/dynamic.h>
#include <folly/json.h>
#include <gtest/gtest.h>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace react_native_linux {
namespace {

/** A daemon whose every notification is clicked as soon as it shows. */
class ClickingServer final : public NotificationServer {
public:
    explicit ClickingServer(Signals signals) : signals_(std::move(signals)) {}

    bool reachable() override { return true; }

    uint32_t notify(const std::string& /*summary*/, const std::string& /*body*/) override {
        clicks_.push_back(++shown_);

        return shown_;
    }

    void close(uint32_t /*serverId*/) override {}

    pollfd pollDescriptor() override { return pollfd{.fd = -1, .events = 0, .revents = 0}; }

    void process() override {
        for (const uint32_t serverId : std::exchange(clicks_, {})) {
            signals_.actionInvoked(serverId, "default");
        }
    }

private:
    Signals signals_;
    uint32_t shown_{0};
    std::vector<uint32_t> clicks_;
};

/**
 * Upstream's `requireNativeModule` on a native platform without Expo's own host: the module from the TurboModule
 * registry, through `createTurboModuleToExpoProxy` (`expo-modules-core/src/TurboModuleToExpoModuleProxy.native.ts`,
 * SDK 58), copied here because this test has no bundler.
 */
constexpr char kRequireNativeModule[] = R"JAVASCRIPT(
const requireNativeModule = (name) => {
  const turboModule = globalThis.nativeModuleProxy[name];
  if (!turboModule) {
    throw new Error(`Cannot find native module '${name}'`);
  }
  const expoModuleProxy = { __turboModule: turboModule };
  Object.keys(Object.getPrototypeOf(turboModule)).forEach((prop) => {
    expoModuleProxy[prop] = turboModule[prop];
  });
  expoModuleProxy.addListener = (eventName, listener) => turboModule[eventName](listener);
  return expoModuleProxy;
};
const settle = (promise) => promise.then((value) => value, (error) => `rejected: ${error.message}`);
const sleep = (milliseconds) => new Promise((resolve) => setTimeout(resolve, milliseconds));
)JAVASCRIPT";

std::string probe(ReactHost& reactHost, const std::string& body) {
    const std::string script = std::string(kRequireNativeModule) + "(async () => {\n" + body +
                               "\n})().then((trace) => { globalThis.trace = JSON.stringify(trace); }, (error) => { "
                               "globalThis.trace = `threw: ${error.message}`; });";

    reactHost.loadScript(std::make_unique<facebook::react::JSBigStdString>(script), "ExpoNotificationsTest.js");

    const std::string trace = awaitGlobalString(reactHost, "trace");

    EXPECT_FALSE(reactHost.hasReportedFatalError());

    return trace;
}

/**
 * #274: every module upstream's `expo-notifications` index requires is registered, and answers what upstream's
 * JavaScript asks of it on Linux: no push token method, so upstream throws its own `UnavailabilityError`, but a push
 * token listener it can subscribe; nothing on the badge, category, background-task and server-registration modules.
 */
TEST(ExpoNotificationsModuleTest, EveryModuleTheIndexRequiresIsRegistered) {
    ReactHost reactHost;
    const folly::dynamic trace = folly::parseJson(probe(reactHost, R"JAVASCRIPT(
const names = ['ExpoBackgroundNotificationTasksModule', 'ExpoBadgeModule', 'ExpoNotificationCategoriesModule',
  'ExpoNotificationPermissionsModule', 'ExpoNotificationPresenter', 'ExpoNotificationScheduler',
  'ExpoNotificationsEmitter', 'ExpoNotificationsHandlerModule', 'ExpoPushTokenManager',
  'NotificationsServerRegistrationModule'];
const methods = Object.fromEntries(names.map((name) => [name,
  Object.keys(requireNativeModule(name)).filter((key) => key !== '__turboModule' && key !== 'addListener').sort()]));
const pushTokens = requireNativeModule('ExpoPushTokenManager');
const subscription = pushTokens.addListener('onDevicePushToken', () => {});
subscription.remove();
return { methods, pushTokenSubscription: typeof subscription.remove };
)JAVASCRIPT"));

    EXPECT_EQ(trace, folly::parseJson(R"({
      "pushTokenSubscription": "function",
      "methods": {
        "ExpoBackgroundNotificationTasksModule": [],
        "ExpoBadgeModule": [],
        "ExpoNotificationCategoriesModule": [],
        "ExpoNotificationPermissionsModule": ["getPermissionsAsync", "requestPermissionsAsync"],
        "ExpoNotificationPresenter": ["dismissAllNotificationsAsync"],
        "ExpoNotificationScheduler": ["scheduleNotificationAsync"],
        "ExpoNotificationsEmitter": ["clearLastNotificationResponse", "emit", "getLastNotificationResponse"],
        "ExpoNotificationsHandlerModule": ["handleNotificationAsync"],
        "ExpoPushTokenManager": [],
        "NotificationsServerRegistrationModule": []
      }
    })"));
}

/**
 * A notification scheduled through `ExpoNotificationScheduler` reaches the handler upstream subscribes as it is
 * imported, shows once the handler answers, and a click on it reaches the response listener and becomes the last
 * response, which `clearLastNotificationResponse` and upstream's `emit` clear.
 */
TEST(ExpoNotificationsModuleTest, SchedulesHandlesShowsAndAnswersAClick) {
    ReactHost reactHost;

    reactHost.notifications().configure(
        "org.example.app", [](const std::string& /*applicationName*/, NotificationServer::Signals signals) {
            return std::make_unique<ClickingServer>(std::move(signals));
        });

    const folly::dynamic trace = folly::parseJson(probe(reactHost, R"JAVASCRIPT(
const emitter = requireNativeModule('ExpoNotificationsEmitter');
const handler = requireNativeModule('ExpoNotificationsHandlerModule');
const scheduler = requireNativeModule('ExpoNotificationScheduler');
const handled = [];
handler.addListener('onHandleNotification', ({ id, notification }) => {
  handled.push(notification.request.content.title);
  handler.handleNotificationAsync(id, { shouldShowBanner: true, shouldShowList: false, shouldPlaySound: false,
    shouldSetBadge: false });
});
const received = new Promise((resolve) => emitter.addListener('onDidReceiveNotification', resolve));
const response = new Promise((resolve) => emitter.addListener('onDidReceiveNotificationResponse', resolve));
let cleared = 0;
emitter.addListener('onDidClearNotificationResponse', () => { cleared += 1; });

const permissions = await requireNativeModule('ExpoNotificationPermissionsModule').requestPermissionsAsync({});
const identifier = await scheduler.scheduleNotificationAsync('turn-4',
  { title: 'Turn complete', body: 'Thread 4 finished', data: { url: 't3://thread/4' } }, null);
const notification = await received;
const clicked = await response;
const last = emitter.getLastNotificationResponse();
emitter.clearLastNotificationResponse();
emitter.emit('onDidClearNotificationResponse');
await sleep(20);
return {
  permissions: permissions.status,
  identifier,
  request: notification.request,
  handled,
  action: clicked.actionIdentifier,
  lastIdentifier: last.notification.request.identifier,
  afterClear: emitter.getLastNotificationResponse(),
  cleared,
  dismissed: await requireNativeModule('ExpoNotificationPresenter').dismissAllNotificationsAsync(),
  unhandled: await settle(handler.handleNotificationAsync('turn-4', {})),
  daily: await settle(scheduler.scheduleNotificationAsync('daily', {}, { type: 'daily', hour: 9, minute: 0 })),
};
)JAVASCRIPT"));

    EXPECT_EQ(trace, folly::parseJson(R"({
      "permissions": "granted",
      "identifier": "turn-4",
      "request": {"identifier": "turn-4", "trigger": null,
                  "content": {"title": "Turn complete", "body": "Thread 4 finished", "data": {"url": "t3://thread/4"}}},
      "handled": ["Turn complete"],
      "action": "expo.modules.notifications.actions.DEFAULT",
      "lastIdentifier": "turn-4",
      "afterClear": null,
      "cleared": 1,
      "dismissed": null,
      "unhandled": "rejected: Failed to handle notification turn-4, it has already been handled.",
      "daily": "rejected: Trigger {\"hour\":9,\"minute\":0,\"type\":\"daily\"} is not supported on Linux, which fires null, a non-repeating timeInterval and a date trigger."
    })"));
}

/** A behavior that shows nothing, and no daemon: permissions stay undetermined and nothing is shown or answered. */
TEST(ExpoNotificationsModuleTest, WithoutADaemonPermissionsAreUndetermined) {
    ReactHost reactHost;
    const folly::dynamic trace = folly::parseJson(probe(reactHost, R"JAVASCRIPT(
const handler = requireNativeModule('ExpoNotificationsHandlerModule');
const emitter = requireNativeModule('ExpoNotificationsEmitter');
const handled = new Promise((resolve) => handler.addListener('onHandleNotification', ({ id }) =>
  handler.handleNotificationAsync(id, null).then(resolve)));
await requireNativeModule('ExpoNotificationScheduler').scheduleNotificationAsync('quiet', { title: 'Quiet' }, null);
return {
  permissions: await requireNativeModule('ExpoNotificationPermissionsModule').getPermissionsAsync(),
  handled: await handled,
  last: emitter.getLastNotificationResponse(),
};
)JAVASCRIPT"));

    EXPECT_EQ(trace, folly::parseJson(R"({
      "permissions": {"status": "undetermined", "granted": false, "canAskAgain": true, "expires": "never"},
      "handled": null,
      "last": null
    })"));
}

TEST(ExpoNotificationsModuleTest, ThrowsWithoutUpstreamsArguments) {
    ReactHost reactHost;

    EXPECT_EQ(probe(reactHost, R"JAVASCRIPT(
const attempt = (call) => { try { call(); return 'returned'; } catch (error) { return error.message; } };
const emitter = requireNativeModule('ExpoNotificationsEmitter');
return [
  attempt(() => emitter.emit()),
  attempt(() => emitter.emit('onSomethingElse')),
  attempt(() => requireNativeModule('ExpoNotificationsHandlerModule').handleNotificationAsync('id')),
  attempt(() => requireNativeModule('ExpoNotificationScheduler').scheduleNotificationAsync('id', {})),
];
)JAVASCRIPT"),
              R"(["ExpoNotificationsEmitter.emit takes an event name",)"
              R"("ExpoNotificationsEmitter has no event onSomethingElse",)"
              R"("ExpoNotificationsHandlerModule.handleNotificationAsync takes an identifier and a behavior",)"
              R"("ExpoNotificationScheduler.scheduleNotificationAsync takes an identifier, a content and a trigger"])");
}

} // namespace
} // namespace react_native_linux
