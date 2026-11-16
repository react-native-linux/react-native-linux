#include "FreedesktopNotifications.h"

#include "FakeNotificationDaemon.h"

#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <gtest/gtest.h>
#include <memory>
#include <poll.h>
#include <stdexcept>
#include <string>
#include <sys/socket.h>
#include <systemd/sd-bus.h>
#include <thread>
#include <unistd.h>
#include <utility>
#include <vector>

namespace react_native_linux {
namespace {

constexpr char kApplication[] = "org.example.app";

struct BusClose {
    void operator()(sd_bus* bus) const noexcept { sd_bus_flush_close_unref(bus); }
};

/** `FakeNotificationDaemon` on the far end of a socket pair, its bus processed on a thread of its own. */
class PeerDaemon {
public:
    explicit PeerDaemon(FakeNotificationDaemon::Behavior behavior) {
        int descriptors[2] = {-1, -1};
        sd_id128_t identifier{};
        sd_bus* server = nullptr;

        EXPECT_EQ(::socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, descriptors), 0);
        clientDescriptor_ = descriptors[1];
        EXPECT_GE(sd_bus_new(&server), 0);
        server_.reset(server);
        EXPECT_GE(sd_bus_set_fd(server, descriptors[0], descriptors[0]), 0);
        EXPECT_GE(sd_id128_randomize(&identifier), 0);
        EXPECT_GE(sd_bus_set_server(server, 1, identifier), 0);
        daemon_ = std::make_unique<FakeNotificationDaemon>(server, std::move(behavior));
        EXPECT_GE(sd_bus_start(server), 0);
        thread_ = std::thread([this] {
            while (!stopping_.load() && sd_bus_process(server_.get(), nullptr) >= 0) {
                sd_bus_wait(server_.get(), 10'000);
            }
        });
    }

    PeerDaemon(const PeerDaemon&) = delete;
    PeerDaemon(PeerDaemon&&) = delete;
    PeerDaemon& operator=(const PeerDaemon&) = delete;
    PeerDaemon& operator=(PeerDaemon&&) = delete;

    ~PeerDaemon() {
        stopping_.store(true);
        thread_.join();
        daemon_.reset();
        ::close(clientDescriptor_);
    }

    [[nodiscard]] FreedesktopNotifications::BusOpener opener() const {
        return [descriptor = clientDescriptor_](sd_bus** bus) {
            const int client = ::dup(descriptor);

            sd_bus_new(bus);
            sd_bus_set_fd(*bus, client, client);

            return sd_bus_start(*bus);
        };
    }

    [[nodiscard]] const FakeNotificationDaemon& daemon() const { return *daemon_; }

private:
    int clientDescriptor_{-1};
    std::unique_ptr<sd_bus, BusClose> server_;
    std::unique_ptr<FakeNotificationDaemon> daemon_;
    std::atomic<bool> stopping_{false};
    std::thread thread_;
};

FakeNotificationDaemon::Behavior behaving(const std::function<void(FakeNotificationDaemon::Behavior&)>& adjust) {
    FakeNotificationDaemon::Behavior behavior;

    adjust(behavior);

    return behavior;
}

struct Received {
    std::vector<std::pair<uint32_t, std::string>> actions;
    std::vector<uint32_t> closed;
};

NotificationServer::Signals recordInto(Received& received) {
    return NotificationServer::Signals{
        .actionInvoked =
            [&received](uint32_t serverId, const std::string& actionKey) {
                received.actions.emplace_back(serverId, actionKey);
            },
        .closed = [&received](uint32_t serverId) { received.closed.push_back(serverId); },
    };
}

/** Polls and processes `server` until `done`, as `NotificationCenter`'s worker does, for at most two seconds. */
void pumpUntil(NotificationServer& server, const std::function<bool()>& done) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);

    while (!done() && std::chrono::steady_clock::now() < deadline) {
        pollfd descriptor = server.pollDescriptor();

        ::poll(&descriptor, 1, 10);
        server.process();
    }
}

TEST(FreedesktopNotificationsTest, AnswersNullWhenTheBusCannotBeOpened) {
    Received received;

    testing::internal::CaptureStderr();
    EXPECT_EQ(
        FreedesktopNotifications::connect([](sd_bus** /*bus*/) { return -ENOENT; }, kApplication, recordInto(received)),
        nullptr);
    EXPECT_EQ(testing::internal::GetCapturedStderr(),
              "[rnl-notifications] no session bus: No such file or directory\n");
}

TEST(FreedesktopNotificationsTest, TheSessionConnectorAnswersNullWithoutASessionBus) {
    const char* previous = std::getenv("DBUS_SESSION_BUS_ADDRESS");
    const std::string saved = previous == nullptr ? "" : previous;
    Received received;

    ::setenv("DBUS_SESSION_BUS_ADDRESS", "unix:path=/nonexistent/rnl-notifications-test-bus", 1);
    testing::internal::CaptureStderr();
    EXPECT_EQ(connectSessionNotifications(kApplication, recordInto(received)), nullptr);
    testing::internal::GetCapturedStderr();

    if (previous == nullptr) {
        ::unsetenv("DBUS_SESSION_BUS_ADDRESS");
    } else {
        ::setenv("DBUS_SESSION_BUS_ADDRESS", saved.c_str(), 1);
    }
}

TEST(FreedesktopNotificationsTest, ShowsANotificationWithTheDefaultActionAndClosesIt) {
    const PeerDaemon peer({});
    Received received;
    const std::unique_ptr<NotificationServer> server =
        FreedesktopNotifications::connect(peer.opener(), kApplication, recordInto(received));

    ASSERT_NE(server, nullptr);
    EXPECT_TRUE(server->reachable());
    EXPECT_EQ(server->notify("Turn complete", "Thread 4 finished"), 1U);
    EXPECT_EQ(server->notify("Second", ""), 2U);

    const std::vector<FakeNotificationDaemon::Notification> shown = peer.daemon().notifications();

    ASSERT_EQ(shown.size(), 2U);
    EXPECT_EQ(shown[0].applicationName, kApplication);
    EXPECT_EQ(shown[0].summary, "Turn complete");
    EXPECT_EQ(shown[0].body, "Thread 4 finished");
    EXPECT_EQ(shown[0].actions, (std::vector<std::string>{"default", "Open"}));
    EXPECT_EQ(shown[0].expireTimeout, -1);

    server->close(1);
    pumpUntil(*server, [&received] { return !received.closed.empty(); });

    EXPECT_EQ(peer.daemon().closed(), std::vector<uint32_t>{1});
    EXPECT_EQ(received.closed, std::vector<uint32_t>{1});
    EXPECT_TRUE(received.actions.empty());
    EXPECT_GE(server->pollDescriptor().fd, 0);
}

TEST(FreedesktopNotificationsTest, DeliversAClickAsActionInvokedAndIgnoresMalformedSignals) {
    const PeerDaemon peer(behaving([](FakeNotificationDaemon::Behavior& behavior) {
        behavior.invokedAction = "default";
        behavior.emitsMalformedSignals = true;
    }));
    Received received;
    const std::unique_ptr<NotificationServer> server =
        FreedesktopNotifications::connect(peer.opener(), kApplication, recordInto(received));

    ASSERT_NE(server, nullptr);
    EXPECT_EQ(server->notify("Turn complete", "Thread 4 finished"), 1U);
    pumpUntil(*server, [&received] { return !received.closed.empty(); });

    EXPECT_EQ(received.actions, (std::vector<std::pair<uint32_t, std::string>>{{1, "default"}}));
    EXPECT_EQ(received.closed, std::vector<uint32_t>{1});
}

TEST(FreedesktopNotificationsTest, AFailingDaemonIsUnreachableAndItsCallsThrow) {
    const PeerDaemon peer(behaving([](FakeNotificationDaemon::Behavior& behavior) { behavior.failsCalls = true; }));
    Received received;
    const std::unique_ptr<NotificationServer> server =
        FreedesktopNotifications::connect(peer.opener(), kApplication, recordInto(received));

    ASSERT_NE(server, nullptr);
    EXPECT_FALSE(server->reachable());
    EXPECT_THROW(
        {
            try {
                server->notify("Turn complete", "");
            } catch (const std::runtime_error& error) {
                EXPECT_STREQ(error.what(), "org.freedesktop.Notifications.Notify: org.freedesktop.DBus.Error.Failed");
                throw;
            }
        },
        std::runtime_error);
    EXPECT_THROW(server->close(1), std::runtime_error);
}

TEST(FreedesktopNotificationsTest, AMalformedNotifyReplyThrows) {
    const PeerDaemon peer(
        behaving([](FakeNotificationDaemon::Behavior& behavior) { behavior.answersMalformed = true; }));
    Received received;
    const std::unique_ptr<NotificationServer> server =
        FreedesktopNotifications::connect(peer.opener(), kApplication, recordInto(received));

    ASSERT_NE(server, nullptr);
    EXPECT_THROW(server->notify("Turn complete", ""), std::runtime_error);
}

} // namespace
} // namespace react_native_linux
