#pragma once

#include <chrono>
#include <cstdint>
#include <deque>
#include <folly/dynamic.h>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <poll.h>
#include <string>
#include <thread>

namespace react_native_linux {

/**
 * The desktop's notification daemon as `NotificationCenter` uses it: `org.freedesktop.Notifications`, which
 * `FreedesktopNotifications` serves over sd-bus. `notify` and `close` throw when the daemon fails or does not answer
 * in time.
 */
class NotificationServer {
public:
    /** The daemon's `ActionInvoked` and `NotificationClosed` signals, delivered from `process`. */
    struct Signals {
        std::function<void(uint32_t serverId, const std::string& actionKey)> actionInvoked;
        std::function<void(uint32_t serverId)> closed;
    };

    NotificationServer() = default;
    NotificationServer(const NotificationServer&) = delete;
    NotificationServer(NotificationServer&&) = delete;
    NotificationServer& operator=(const NotificationServer&) = delete;
    NotificationServer& operator=(NotificationServer&&) = delete;
    virtual ~NotificationServer() = default;

    [[nodiscard]] virtual bool reachable() = 0;
    virtual uint32_t notify(const std::string& summary, const std::string& body) = 0;
    virtual void close(uint32_t serverId) = 0;
    /** What to poll before `process` has more to deliver. */
    [[nodiscard]] virtual pollfd pollDescriptor() = 0;
    virtual void process() = 0;
};

/** Connects to the session's daemon for `applicationName`, or answers null when the session has no bus. */
using NotificationServerConnector =
    std::function<std::unique_ptr<NotificationServer>(const std::string& applicationName, NotificationServer::Signals)>;

/**
 * What expo-notifications' native modules do on Linux (#274): local notifications delivered on their trigger,
 * offered to the app's notification handler, and shown through the daemon when the handler says so; a click on one
 * becomes the last notification response. Events reach JavaScript through `emit`, named as upstream's modules name
 * them.
 *
 * - A delivered notification is emitted as `onDidReceiveNotification`. When `onHandleNotification` has a listener,
 *   which upstream's default handler is until the app passes `null` to `setNotificationHandler`, it is offered there
 *   and shown once `handle` answers a behavior that shows a banner or a list entry. A handler that does not answer
 *   within `handlerTimeout` gets `onHandleNotificationTimeout` and the notification shows, as upstream documents. With
 *   no listener it does not show, as on Android and iOS. A delivery whose identifier is still awaiting the handler
 *   times the earlier one out first, so each delivery gets one decision.
 * - Triggers: `null` (now), `timeInterval` without `repeats`, and `date`. Every other one rejects.
 * - Permissions are granted while a daemon answers on the session bus, and undetermined otherwise.
 *
 * Threading contract: `configure` runs before every other call, on the thread that constructs the center. Every
 * other public method may then be called from any thread and never blocks on the bus. The work, the daemon
 * connection and the timers live on one worker thread, which the first of them starts; `resolve`, `reject` and
 * `emit` are called from it, and `hasListeners` is asked from it. `lastResponse` and `clearLastResponse` are guarded
 * by a mutex, because upstream reads the last response synchronously on the JavaScript thread. The destructor stops
 * the worker and drops whatever is scheduled.
 */
class NotificationCenter final {
public:
    using Emit = std::function<void(const std::string& eventName, folly::dynamic payload)>;
    using HasListeners = std::function<bool(const std::string& eventName)>;
    using Resolve = std::function<void(folly::dynamic value)>;
    using Reject = std::function<void(const std::string& message)>;

    static constexpr std::chrono::milliseconds kHandlerTimeout{3000};

    /** Throws `std::runtime_error`, naming the system error, when the worker's wake descriptor cannot be created. */
    NotificationCenter(Emit emit, HasListeners hasListeners,
                       std::chrono::milliseconds handlerTimeout = kHandlerTimeout);
    NotificationCenter(const NotificationCenter&) = delete;
    NotificationCenter(NotificationCenter&&) = delete;
    NotificationCenter& operator=(const NotificationCenter&) = delete;
    NotificationCenter& operator=(NotificationCenter&&) = delete;
    ~NotificationCenter();

    /** Before any other call, as `WindowSession` makes it before the bundle runs; without it no notification shows. */
    void configure(std::string applicationName, NotificationServerConnector connect);

    void getPermissions(Resolve resolve);
    void schedule(std::string identifier, folly::dynamic content, folly::dynamic trigger, Resolve resolve,
                  Reject reject);
    /** Upstream's `handleNotificationAsync`: `shows` is whether the handler's behavior shows a banner or list entry. */
    void handle(std::string identifier, bool shows, Resolve resolve, Reject reject);
    void dismissAll(Resolve resolve);

    /** The last response, or null. */
    [[nodiscard]] folly::dynamic lastResponse() const;
    void clearLastResponse();

private:
    using Clock = std::chrono::steady_clock;
    using Task = std::function<void()>;

    struct Pending {
        Clock::time_point deadline;
        folly::dynamic notification;
    };

    void post(Task task);
    void run();
    void waitForWork();
    void fireDueTimers();
    void deliver(const folly::dynamic& notification);
    void timeOut(const folly::dynamic& notification);
    void present(const folly::dynamic& notification);
    NotificationServer* server();
    void onActionInvoked(uint32_t serverId, const std::string& actionKey);

    Emit emit_;
    HasListeners hasListeners_;
    std::chrono::milliseconds handlerTimeout_;

    std::mutex mutex_;
    std::deque<Task> tasks_;
    bool stopping_{false};
    int wakeDescriptor_;
    std::thread thread_;

    mutable std::mutex responseMutex_;
    folly::dynamic lastResponse_{nullptr};

    std::string applicationName_;
    NotificationServerConnector connect_;
    std::unique_ptr<NotificationServer> server_;
    std::map<std::string, Pending> scheduled_;
    std::map<std::string, Pending> handling_;
    std::map<uint32_t, folly::dynamic> presented_;
};

} // namespace react_native_linux
