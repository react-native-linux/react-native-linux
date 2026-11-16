#include "NotificationCenter.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <folly/dynamic.h>
#include <folly/json.h>
#include <functional>
#include <future>
#include <gtest/gtest.h>
#include <memory>
#include <mutex>
#include <optional>
#include <poll.h>
#include <stdexcept>
#include <string>
#include <sys/eventfd.h>
#include <unistd.h>
#include <utility>
#include <vector>

namespace react_native_linux {
namespace {

using std::chrono::milliseconds;

constexpr milliseconds kWait{2000};
constexpr char kDefaultAction[] = "expo.modules.notifications.actions.DEFAULT";

/**
 * What the scripted daemon is told and what it answers, shared between the test thread and the center's worker,
 * which owns the server. `click` and `closeFromDaemon` queue a signal and wake the worker through the descriptor the
 * server polls, as a real daemon's signal would.
 */
struct Script {
    Script() = default;
    Script(const Script&) = delete;
    Script(Script&&) = delete;
    Script& operator=(const Script&) = delete;
    Script& operator=(Script&&) = delete;

    ~Script() { ::close(wake); }

    void click(uint32_t serverId, const std::string& actionKey) {
        {
            const std::lock_guard lock(mutex);

            pendingSignals.emplace_back(serverId, actionKey);
        }
        signal();
    }

    void closeFromDaemon(uint32_t serverId) {
        {
            const std::lock_guard lock(mutex);

            pendingSignals.emplace_back(serverId, std::nullopt);
        }
        signal();
    }

    void signal() const {
        const uint64_t one = 1;

        ::write(wake, &one, sizeof one);
    }

    std::vector<std::string> shown() {
        const std::lock_guard lock(mutex);

        return summaries;
    }

    std::vector<uint32_t> closedByCenter() {
        const std::lock_guard lock(mutex);

        return closed;
    }

    std::atomic<bool> reachable{true};
    std::atomic<bool> failsNotify{false};
    std::atomic<bool> failsClose{false};
    int wake{::eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK)};
    std::mutex mutex;
    std::vector<std::string> summaries;
    std::vector<uint32_t> closed;
    /** In the daemon's order: an action key for `ActionInvoked`, none for `NotificationClosed`. */
    std::vector<std::pair<uint32_t, std::optional<std::string>>> pendingSignals;
};

class ScriptedServer final : public NotificationServer {
public:
    ScriptedServer(std::shared_ptr<Script> script, Signals signals)
        : script_(std::move(script)), signals_(std::move(signals)) {}

    bool reachable() override { return script_->reachable; }

    uint32_t notify(const std::string& summary, const std::string& body) override {
        if (script_->failsNotify) {
            throw std::runtime_error("Notify failed");
        }

        const std::lock_guard lock(script_->mutex);

        script_->summaries.push_back(summary + "|" + body);

        return static_cast<uint32_t>(script_->summaries.size());
    }

    void close(uint32_t serverId) override {
        if (script_->failsClose) {
            throw std::runtime_error("CloseNotification failed");
        }

        const std::lock_guard lock(script_->mutex);

        script_->closed.push_back(serverId);
    }

    pollfd pollDescriptor() override { return pollfd{.fd = script_->wake, .events = POLLIN, .revents = 0}; }

    void process() override {
        uint64_t wakes = 0;
        std::vector<std::pair<uint32_t, std::optional<std::string>>> pending;

        ::read(script_->wake, &wakes, sizeof wakes);
        {
            const std::lock_guard lock(script_->mutex);

            pending.swap(script_->pendingSignals);
        }

        for (const auto& [serverId, actionKey] : pending) {
            if (actionKey.has_value()) {
                signals_.actionInvoked(serverId, actionKey.value());
            } else {
                signals_.closed(serverId);
            }
        }
    }

private:
    std::shared_ptr<Script> script_;
    Signals signals_;
};

/** Every event the center emitted, in order, and whether the handler event has a listener. */
class Events {
public:
    void record(const std::string& name, folly::dynamic payload) {
        const std::lock_guard lock(mutex_);

        events_.emplace_back(name, std::move(payload));
        changed_.notify_all();
    }

    /** Waits until `count` events have been emitted, and answers them. */
    std::vector<std::pair<std::string, folly::dynamic>> await(size_t count) {
        std::unique_lock lock(mutex_);

        changed_.wait_for(lock, kWait, [this, count] { return events_.size() >= count; });

        return events_;
    }

    std::vector<std::string> names() {
        const std::lock_guard lock(mutex_);
        std::vector<std::string> names;

        for (const auto& [name, payload] : events_) {
            names.push_back(name);
        }

        return names;
    }

    std::atomic<bool> handlerListens{true};

private:
    std::mutex mutex_;
    std::condition_variable changed_;
    std::vector<std::pair<std::string, folly::dynamic>> events_;
};

/** A resolve and reject pair whose outcome the test waits for: the resolved value as JSON, or `rejected: <message>`. */
struct Outcome {
    NotificationCenter::Resolve resolve() {
        return [promise = promise_](folly::dynamic value) { promise->set_value(folly::toJson(value)); };
    }

    NotificationCenter::Reject reject() {
        return [promise = promise_](const std::string& message) { promise->set_value("rejected: " + message); };
    }

    std::string get() {
        std::future<std::string> future = promise_->get_future();

        return future.wait_for(kWait) == std::future_status::ready ? future.get() : "never settled";
    }

    std::shared_ptr<std::promise<std::string>> promise_{std::make_shared<std::promise<std::string>>()};
};

class NotificationCenterTest : public ::testing::Test {
protected:
    std::unique_ptr<NotificationCenter> makeCenter(milliseconds handlerTimeout = NotificationCenter::kHandlerTimeout) {
        return std::make_unique<NotificationCenter>(
            [this](const std::string& name, folly::dynamic payload) { events_.record(name, std::move(payload)); },
            [this](const std::string& name) {
                EXPECT_EQ(name, "onHandleNotification");

                return events_.handlerListens.load();
            },
            handlerTimeout);
    }

    void connect(NotificationCenter& center) {
        center.configure("org.example.app",
                         [script = script_](const std::string& applicationName, NotificationServer::Signals signals) {
                             EXPECT_EQ(applicationName, "org.example.app");

                             return std::make_unique<ScriptedServer>(script, std::move(signals));
                         });
    }

    static std::string schedule(
        NotificationCenter& center, const std::string& identifier, const folly::dynamic& trigger,
        const folly::dynamic& content = folly::dynamic::object("title", "Turn complete")("body", "Thread 4 finished")) {
        Outcome outcome;

        center.schedule(identifier, content, trigger, outcome.resolve(), outcome.reject());

        return outcome.get();
    }

    static std::string handle(NotificationCenter& center, const std::string& identifier, bool shows) {
        Outcome outcome;

        center.handle(identifier, shows, outcome.resolve(), outcome.reject());

        return outcome.get();
    }

    static std::string permissions(NotificationCenter& center) {
        Outcome outcome;

        center.getPermissions(outcome.resolve());

        return outcome.get();
    }

    static std::string dismissAll(NotificationCenter& center) {
        Outcome outcome;

        center.dismissAll(outcome.resolve());

        return outcome.get();
    }

    /** Schedules `identifier` now, answers the handler with `shows`, and waits for the daemon to have seen it or not.
     */
    void deliverAndHandle(NotificationCenter& center, const std::string& identifier, bool shows) {
        const size_t before = events_.names().size();

        EXPECT_EQ(schedule(center, identifier, nullptr), "\"" + identifier + "\"");
        events_.await(before + 2);
        EXPECT_EQ(handle(center, identifier, shows), "null");
    }

    Events events_;
    std::shared_ptr<Script> script_{std::make_shared<Script>()};
};

TEST_F(NotificationCenterTest, AnUnusedCenterStartsNoWorker) { EXPECT_NE(makeCenter(), nullptr); }

TEST_F(NotificationCenterTest, PermissionsAreGrantedOnlyWhileADaemonAnswers) {
    const std::string undetermined =
        R"({"canAskAgain":true,"expires":"never","granted":false,"status":"undetermined"})";
    const std::unique_ptr<NotificationCenter> unconfigured = makeCenter();
    const std::unique_ptr<NotificationCenter> withoutBus = makeCenter();
    const std::unique_ptr<NotificationCenter> center = makeCenter();

    EXPECT_EQ(folly::parseJson(permissions(*unconfigured)), folly::parseJson(undetermined));

    withoutBus->configure("org.example.app", [](const std::string& /*applicationName*/,
                                                NotificationServer::Signals /*signals*/) { return nullptr; });
    EXPECT_EQ(folly::parseJson(permissions(*withoutBus)), folly::parseJson(undetermined));

    connect(*center);
    EXPECT_EQ(folly::parseJson(permissions(*center)),
              folly::parseJson(R"({"canAskAgain":true,"expires":"never","granted":true,"status":"granted"})"));
    script_->reachable = false;
    EXPECT_EQ(folly::parseJson(permissions(*center)), folly::parseJson(undetermined));
}

TEST_F(NotificationCenterTest, OffersADeliveredNotificationToTheHandlerAndShowsIt) {
    const std::unique_ptr<NotificationCenter> center = makeCenter();

    connect(*center);
    EXPECT_EQ(schedule(*center, "turn-4", nullptr,
                       folly::dynamic::object("title", "Turn complete")("body", "Thread 4 finished")(
                           "data", folly::dynamic::object("url", "t3://thread/4"))),
              "\"turn-4\"");

    const auto events = events_.await(2);

    ASSERT_EQ(events.size(), 2U);
    EXPECT_EQ(events[0].first, "onDidReceiveNotification");
    EXPECT_EQ(events[0].second["request"],
              folly::parseJson(R"({"identifier":"turn-4","trigger":null,"content":{"title":"Turn complete",)"
                               R"("body":"Thread 4 finished","data":{"url":"t3://thread/4"}}})"));
    EXPECT_TRUE(events[0].second["date"].isDouble());
    EXPECT_EQ(events[1].first, "onHandleNotification");
    EXPECT_EQ(events[1].second["id"], "turn-4");
    EXPECT_EQ(events[1].second["notification"], events[0].second);
    EXPECT_TRUE(script_->shown().empty());

    EXPECT_EQ(handle(*center, "turn-4", true), "null");
    EXPECT_EQ(script_->shown(), std::vector<std::string>{"Turn complete|Thread 4 finished"});
    EXPECT_EQ(handle(*center, "turn-4", true), "rejected: Failed to handle notification turn-4, it has already been "
                                               "handled.");
}

TEST_F(NotificationCenterTest, AClickBecomesTheLastResponse) {
    const std::unique_ptr<NotificationCenter> center = makeCenter();

    connect(*center);
    EXPECT_TRUE(center->lastResponse().isNull());
    deliverAndHandle(*center, "turn-4", true);
    script_->click(1, "default");

    const auto events = events_.await(3);

    ASSERT_EQ(events.size(), 3U);
    EXPECT_EQ(events[2].first, "onDidReceiveNotificationResponse");
    EXPECT_EQ(events[2].second["actionIdentifier"], kDefaultAction);
    EXPECT_EQ(events[2].second["notification"], events[0].second);
    EXPECT_EQ(center->lastResponse(), events[2].second);

    script_->click(1, "reply");
    EXPECT_EQ(events_.await(4).at(3).second["actionIdentifier"], "reply");

    center->clearLastResponse();
    EXPECT_TRUE(center->lastResponse().isNull());
}

TEST_F(NotificationCenterTest, ANotificationTheHandlerHidesOrNobodyHandlesIsNotShown) {
    const std::unique_ptr<NotificationCenter> center = makeCenter();

    connect(*center);
    deliverAndHandle(*center, "hidden", false);
    events_.handlerListens = false;
    EXPECT_EQ(schedule(*center, "unhandled", nullptr), "\"unhandled\"");
    events_.await(3);

    EXPECT_EQ(events_.names(), (std::vector<std::string>{"onDidReceiveNotification", "onHandleNotification",
                                                         "onDidReceiveNotification"}));
    EXPECT_EQ(handle(*center, "unhandled", true),
              "rejected: Failed to handle notification unhandled, it has already been handled.");
    EXPECT_TRUE(script_->shown().empty());
}

TEST_F(NotificationCenterTest, AHandlerThatDoesNotAnswerTimesOutAndTheNotificationShows) {
    const std::unique_ptr<NotificationCenter> center = makeCenter(milliseconds(20));

    connect(*center);
    EXPECT_EQ(schedule(*center, "slow", nullptr, folly::dynamic::object("title", "Untitled body")), "\"slow\"");

    const auto events = events_.await(3);

    ASSERT_EQ(events.size(), 3U);
    EXPECT_EQ(events[2].first, "onHandleNotificationTimeout");
    EXPECT_EQ(events[2].second["id"], "slow");
    EXPECT_EQ(events[2].second["notification"], events[0].second);
    EXPECT_EQ(dismissAll(*center), "null");
    EXPECT_EQ(script_->shown(), std::vector<std::string>{"Untitled body|"});
}

TEST_F(NotificationCenterTest, FiresTimeIntervalAndDateTriggersWhenTheyAreDue) {
    const std::unique_ptr<NotificationCenter> center = makeCenter();
    const auto startedAt = std::chrono::steady_clock::now();
    const double now = static_cast<double>(
        std::chrono::duration_cast<milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count());

    events_.handlerListens = false;
    EXPECT_EQ(schedule(*center, "interval",
                       folly::dynamic::object("type", "timeInterval")("seconds", 0.05)("repeats", false)),
              "\"interval\"");
    EXPECT_EQ(schedule(*center, "past", folly::dynamic::object("type", "date")("timestamp", now - 1000)), "\"past\"");
    EXPECT_EQ(schedule(*center, "later", folly::dynamic::object("type", "date")("timestamp", now + 100)), "\"later\"");

    const auto events = events_.await(3);

    ASSERT_EQ(events.size(), 3U);
    EXPECT_EQ(events[0].second["request"]["identifier"], "past");
    EXPECT_EQ(events[1].second["request"]["identifier"], "interval");
    EXPECT_EQ(events[1].second["request"]["trigger"]["seconds"], 0.05);
    EXPECT_EQ(events[2].second["request"]["identifier"], "later");
    EXPECT_GE(std::chrono::steady_clock::now() - startedAt, milliseconds(50));
}

TEST_F(NotificationCenterTest, RejectsTriggersAndContentLinuxCannotFire) {
    const std::unique_ptr<NotificationCenter> center = makeCenter();

    EXPECT_EQ(
        schedule(*center, "repeating", folly::dynamic::object("type", "timeInterval")("seconds", 60)("repeats", true)),
        R"(rejected: Trigger {"repeats":true,"seconds":60,"type":"timeInterval"} is not supported on Linux, )"
        R"(which fires null, a non-repeating timeInterval and a date trigger.)");
    EXPECT_EQ(schedule(*center, "daily", folly::dynamic::object("type", "daily")("hour", 9)("minute", 0)),
              R"(rejected: Trigger {"hour":9,"minute":0,"type":"daily"} is not supported on Linux, which fires )"
              R"(null, a non-repeating timeInterval and a date trigger.)");
    EXPECT_NE(schedule(*center, "no-seconds", folly::dynamic::object("type", "timeInterval")).find("rejected: "),
              std::string::npos);
    EXPECT_EQ(schedule(*center, "no-content", nullptr, "text"),
              "rejected: A notification's content must be an object.");
    EXPECT_TRUE(events_.names().empty());
}

TEST_F(NotificationCenterTest, ANotificationIsDeliveredButNotShownWithoutABusOrWhenNotifyFails) {
    const std::unique_ptr<NotificationCenter> unconfigured = makeCenter();
    const std::unique_ptr<NotificationCenter> center = makeCenter();

    testing::internal::CaptureStderr();
    deliverAndHandle(*unconfigured, "nowhere", true);
    connect(*center);
    script_->failsNotify = true;
    deliverAndHandle(*center, "failing", true);

    EXPECT_EQ(testing::internal::GetCapturedStderr(),
              "[rnl-notifications] no session bus: nowhere is not shown\n[rnl-notifications] Notify failed\n");
    EXPECT_TRUE(script_->shown().empty());
    EXPECT_EQ(dismissAll(*center), "null");
    EXPECT_TRUE(script_->closedByCenter().empty());
}

TEST_F(NotificationCenterTest, DismissAllClosesEveryShownNotificationAndForgetsIt) {
    const std::unique_ptr<NotificationCenter> center = makeCenter();

    connect(*center);
    deliverAndHandle(*center, "first", true);
    deliverAndHandle(*center, "second", true);
    EXPECT_EQ(dismissAll(*center), "null");
    EXPECT_EQ(script_->closedByCenter(), (std::vector<uint32_t>{1, 2}));

    script_->click(1, "default");
    deliverAndHandle(*center, "third", true);
    script_->failsClose = true;
    testing::internal::CaptureStderr();
    EXPECT_EQ(dismissAll(*center), "null");
    EXPECT_EQ(testing::internal::GetCapturedStderr(), "[rnl-notifications] CloseNotification failed\n");

    script_->click(3, "default");
    deliverAndHandle(*center, "fourth", true);
    EXPECT_EQ(events_.names().size(), 8U);
    EXPECT_TRUE(center->lastResponse().isNull());
}

TEST_F(NotificationCenterTest, ANotificationTheDaemonClosedNoLongerAnswersAClick) {
    const std::unique_ptr<NotificationCenter> center = makeCenter();

    connect(*center);
    deliverAndHandle(*center, "closed", true);
    script_->closeFromDaemon(1);
    script_->click(1, "default");
    deliverAndHandle(*center, "after", true);

    EXPECT_EQ(events_.names(), (std::vector<std::string>{"onDidReceiveNotification", "onHandleNotification",
                                                         "onDidReceiveNotification", "onHandleNotification"}));
    EXPECT_TRUE(center->lastResponse().isNull());
}

TEST_F(NotificationCenterTest, DestroyingTheCenterDropsWhatIsStillScheduled) {
    const std::unique_ptr<NotificationCenter> center = makeCenter();

    EXPECT_EQ(schedule(*center, "tomorrow", folly::dynamic::object("type", "timeInterval")("seconds", 86400)),
              "\"tomorrow\"");
}

} // namespace
} // namespace react_native_linux
