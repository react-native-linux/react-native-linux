#include "OpenUriPortal.h"

#include "PeerBus.h"

#include <array>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <future>
#include <gtest/gtest.h>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <sys/socket.h>
#include <sys/stat.h>
#include <systemd/sd-bus.h>
#include <thread>
#include <unistd.h>
#include <vector>

namespace {

using react_native_linux::OpenUriPortal;
using react_native_linux::test_support::failingWith;
using react_native_linux::test_support::failureOf;
using react_native_linux::test_support::PeerBus;
using std::chrono::milliseconds;

constexpr milliseconds kShortTimeout(200);

/** What `FakeOpenUriPortal` saw of one `OpenFile` call. */
struct OpenFileCall {
    std::string parentWindow;
    std::string contents;
    bool readOnly{false};
    std::string option;
    bool ask{false};
};

/**
 * An xdg-desktop-portal on a `PeerBus`: `OpenURI`'s `version` property and its `OpenFile` method, which reads the
 * file through the descriptor it is handed. It can lack the interface, fail the call, never answer, or stop reading
 * after it answered `version`. It keeps hold of every client that connects, so a test can see whether its caller is
 * still on the bus, as the real portal sees it.
 */
class FakeOpenUriPortal {
public:
    enum class Behavior : std::uint8_t { Serves, LacksOpenUri, Fails, NeverAnswers, StopsReadingAfterVersion };

    explicit FakeOpenUriPortal(Behavior behavior)
        : behavior_(behavior), peer_([this](sd_bus_message* message) { return answer(message); }) {}

    /**
     * Connects a client, with the smallest send buffer the kernel allows when the portal is to stop reading, so a call
     * the portal no longer reads stays queued on any host.
     */
    [[nodiscard]] react_native_linux::SdBusOpener opener() {
        return [this, open = peer_.opener()](sd_bus** bus) {
            const int opened = open(bus);
            const int smallest = 1;
            const std::lock_guard<std::mutex> lock(mutex_);

            if (behavior_ == Behavior::StopsReadingAfterVersion) {
                EXPECT_EQ(::setsockopt(sd_bus_get_fd(*bus), SOL_SOCKET, SO_SNDBUF, &smallest, sizeof(smallest)), 0);
            }

            clients_.emplace_back(sd_bus_ref(*bus));

            return opened;
        };
    }

    [[nodiscard]] std::optional<OpenFileCall> lastOpenFile() {
        const std::lock_guard<std::mutex> lock(mutex_);

        return lastOpenFile_;
    }

    /** How many connections callers opened. */
    [[nodiscard]] std::size_t connections() {
        const std::lock_guard<std::mutex> lock(mutex_);

        return clients_.size();
    }

    /** How many of those are still open. */
    [[nodiscard]] int connectedClients() {
        const std::lock_guard<std::mutex> lock(mutex_);
        int connected = 0;

        for (const auto& client : clients_) {
            connected += sd_bus_is_open(client.get()) > 0 ? 1 : 0;
        }

        return connected;
    }

    /** Closes every client's connection, as sd-bus does once the bus it was on goes away. */
    void disconnectClients() {
        const std::lock_guard<std::mutex> lock(mutex_);

        for (const auto& client : clients_) {
            sd_bus_close(client.get());
        }
    }

    /** Whether the newest client has output the portal has not read. */
    [[nodiscard]] bool clientHasQueuedOutput() {
        const std::lock_guard<std::mutex> lock(mutex_);
        std::uint64_t queued = 0;

        return !clients_.empty() && sd_bus_get_n_queued_write(clients_.back().get(), &queued) >= 0 && queued > 0;
    }

private:
    struct BusUnref {
        void operator()(sd_bus* bus) const noexcept { sd_bus_unref(bus); }
    };

    bool answer(sd_bus_message* message) {
        const std::lock_guard<std::mutex> lock(mutex_);

        if (behavior_ == Behavior::NeverAnswers) {
            return true;
        }

        if (behavior_ == Behavior::Fails) {
            sd_bus_reply_method_errorf(message, SD_BUS_ERROR_ACCESS_DENIED, "injected failure");
        } else if (sd_bus_message_is_method_call(message, "org.freedesktop.DBus.Properties", "Get") > 0) {
            answerVersion(message);

            return behavior_ != Behavior::StopsReadingAfterVersion;
        } else if (sd_bus_message_is_method_call(message, "org.freedesktop.portal.OpenURI", "OpenFile") > 0) {
            answerOpenFile(message);
        } else {
            sd_bus_reply_method_errorf(message, SD_BUS_ERROR_UNKNOWN_METHOD, "unexpected call");
        }

        return true;
    }

    void answerVersion(sd_bus_message* message) const {
        const char* interface = nullptr;
        const char* property = nullptr;

        sd_bus_message_read(message, "ss", &interface, &property);

        if (behavior_ == Behavior::LacksOpenUri || std::string(interface) != "org.freedesktop.portal.OpenURI" ||
            std::string(property) != "version") {
            sd_bus_reply_method_errorf(message, SD_BUS_ERROR_UNKNOWN_INTERFACE, "no such interface");
        } else {
            sd_bus_reply_method_return(message, "v", "u", 4);
        }
    }

    void answerOpenFile(sd_bus_message* message) {
        const char* parentWindow = nullptr;
        int descriptor = -1;
        const char* option = nullptr;
        int ask = 0;
        std::array<char, 64> buffer{};

        sd_bus_message_read(message, "sha{sv}", &parentWindow, &descriptor, 1, &option, "b", &ask);

        const ssize_t length = ::pread(descriptor, buffer.data(), buffer.size(), 0);

        lastOpenFile_ = OpenFileCall{.parentWindow = parentWindow,
                                     .contents = std::string(buffer.data(), length < 0 ? 0 : length),
                                     .readOnly = (::fcntl(descriptor, F_GETFL) & O_ACCMODE) == O_RDONLY,
                                     .option = option,
                                     .ask = ask != 0};
        sd_bus_reply_method_return(message, "o", "/org/freedesktop/portal/desktop/request/1_1/rnl");
    }

    Behavior behavior_;
    std::mutex mutex_;
    std::optional<OpenFileCall> lastOpenFile_;
    std::vector<std::unique_ptr<sd_bus, BusUnref>> clients_;
    // Last, so its thread stops before anything `answer` reads goes.
    PeerBus peer_;
};

class OpenUriPortalTest : public ::testing::Test {
protected:
    void SetUp() override {
        file_ = std::filesystem::temp_directory_path() /
                ("rnl-open-uri-portal-" + std::string(::testing::UnitTest::GetInstance()->current_test_info()->name()));
        std::ofstream(file_) << "attachment bytes";
    }

    void TearDown() override { std::filesystem::remove(file_); }

    std::filesystem::path file_;
};

/** #744: a portal that answers `OpenURI`'s `version` can share; one without the interface cannot. */
TEST_F(OpenUriPortalTest, APortalThatServesOpenUriIsAvailable) {
    FakeOpenUriPortal serving(FakeOpenUriPortal::Behavior::Serves);
    FakeOpenUriPortal lacking(FakeOpenUriPortal::Behavior::LacksOpenUri);

    EXPECT_TRUE(OpenUriPortal(serving.opener()).isAvailable());
    EXPECT_FALSE(OpenUriPortal(lacking.opener()).isAvailable());
}

/** No session bus, a portal that fails, or one that never answers is unavailable, within the timeout. */
TEST_F(OpenUriPortalTest, NoBusAFailingPortalOrASilentOneIsUnavailableWithoutHanging) {
    FakeOpenUriPortal failing(FakeOpenUriPortal::Behavior::Fails);
    FakeOpenUriPortal silent(FakeOpenUriPortal::Behavior::NeverAnswers);
    const auto started = std::chrono::steady_clock::now();

    EXPECT_FALSE(OpenUriPortal(failingWith(-ENOENT)).isAvailable());
    EXPECT_FALSE(OpenUriPortal(failing.opener()).isAvailable());
    EXPECT_FALSE(OpenUriPortal(silent.opener(), kShortTimeout).isAvailable());
    EXPECT_LT(std::chrono::steady_clock::now() - started, std::chrono::seconds(2));
}

/** The file reaches the portal as a read-only descriptor, with no parent window and the chooser asked for. */
TEST_F(OpenUriPortalTest, TheFileIsHandedToThePortalsChooserAsAReadOnlyDescriptor) {
    FakeOpenUriPortal portal(FakeOpenUriPortal::Behavior::Serves);

    OpenUriPortal(portal.opener()).openFile(file_);

    const std::optional<OpenFileCall> call = portal.lastOpenFile();

    ASSERT_TRUE(call.has_value());
    EXPECT_EQ(call->parentWindow, "");
    EXPECT_EQ(call->contents, "attachment bytes");
    EXPECT_TRUE(call->readOnly);
    EXPECT_EQ(call->option, "ask");
    EXPECT_TRUE(call->ask);
}

/**
 * The caller stays on the bus after the portal accepted the share, since the portal closes the request, and dismisses
 * the chooser, once its caller leaves. Every call shares the one connection, which closes when the portal object goes.
 */
TEST_F(OpenUriPortalTest, TheCallerStaysOnTheBusAfterAShareUntilThePortalObjectGoes) {
    FakeOpenUriPortal fake(FakeOpenUriPortal::Behavior::Serves);
    auto portal = std::make_unique<OpenUriPortal>(fake.opener());

    portal->openFile(file_);
    std::this_thread::sleep_for(milliseconds(50));
    ASSERT_EQ(fake.connectedClients(), 1);
    EXPECT_TRUE(portal->isAvailable());
    EXPECT_EQ(fake.connections(), 1U);

    portal.reset();
    EXPECT_EQ(fake.connectedClients(), 0);
}

/** A connection that closed, as one does when its bus goes away, is opened again, to the bus now there. */
TEST_F(OpenUriPortalTest, AClosedConnectionIsOpenedAgainForTheNextCall) {
    FakeOpenUriPortal gone(FakeOpenUriPortal::Behavior::Serves);
    FakeOpenUriPortal back(FakeOpenUriPortal::Behavior::Serves);
    OpenUriPortal portal([openGone = gone.opener(), openBack = back.opener(), opened = 0](sd_bus** bus) mutable {
        return ++opened == 1 ? openGone(bus) : openBack(bus);
    });

    EXPECT_TRUE(portal.isAvailable());
    gone.disconnectClients();
    EXPECT_TRUE(portal.isAvailable());
    EXPECT_EQ(gone.connections(), 1U);
    EXPECT_EQ(back.connectedClients(), 1);
}

/**
 * #745's lesson: letting the portal object go after the portal stopped reading, with calls still waiting to be
 * written, returns at once instead of waiting forever for the portal to read them.
 */
TEST_F(OpenUriPortalTest, LettingGoAfterThePortalStoppedReadingReturnsAtOnce) {
    // Outlives `deaf`, whose closing end releases a teardown that hangs, so a regression fails here rather than hangs.
    std::future<void> lettingGo;
    FakeOpenUriPortal deaf(FakeOpenUriPortal::Behavior::StopsReadingAfterVersion);
    auto portal = std::make_unique<OpenUriPortal>(deaf.opener(), milliseconds(10));

    EXPECT_TRUE(portal->isAvailable());

    for (int attempt = 0; attempt < 256 && !deaf.clientHasQueuedOutput(); ++attempt) {
        EXPECT_FALSE(portal->isAvailable());
    }

    ASSERT_TRUE(deaf.clientHasQueuedOutput());

    lettingGo = std::async(std::launch::async, [stalled = std::move(portal)]() mutable { stalled.reset(); });
    EXPECT_EQ(lettingGo.wait_for(std::chrono::seconds(2)), std::future_status::ready);
}

/** A missing file, no session bus, a refusing portal or a silent one fails the share with the reason. */
TEST_F(OpenUriPortalTest, AShareThatCannotReachTheChooserFailsWithItsReason) {
    FakeOpenUriPortal failing(FakeOpenUriPortal::Behavior::Fails);
    FakeOpenUriPortal silent(FakeOpenUriPortal::Behavior::NeverAnswers);
    const std::filesystem::path missing = file_.string() + ".missing";

    EXPECT_EQ(failureOf([&] { OpenUriPortal(failing.opener()).openFile(missing); }),
              "cannot open " + missing.string() + ": No such file or directory");
    EXPECT_EQ(failureOf([&] { OpenUriPortal(failingWith(-ENOMEDIUM)).openFile(file_); }),
              "cannot open the session bus: No medium found");
    EXPECT_EQ(failureOf([&] { OpenUriPortal(failing.opener()).openFile(file_); }),
              "the OpenURI portal refused it: org.freedesktop.DBus.Error.AccessDenied");
    EXPECT_EQ(failureOf([&] { OpenUriPortal(silent.opener(), kShortTimeout).openFile(file_); }),
              "the OpenURI portal refused it: org.freedesktop.DBus.Error.Timeout");
}

/**
 * Only a regular file is shared. A FIFO nobody writes to fails at once rather than leave the worker waiting for a
 * writer forever, and a directory fails too.
 */
TEST_F(OpenUriPortalTest, AnythingButARegularFileFailsAtOnce) {
    FakeOpenUriPortal fake(FakeOpenUriPortal::Behavior::Serves);
    OpenUriPortal portal(fake.opener());
    const std::filesystem::path fifo = file_.string() + ".fifo";

    ASSERT_EQ(::mkfifo(fifo.c_str(), 0600), 0);

    std::future<std::string> sharing =
        std::async(std::launch::async, [&] { return failureOf([&] { portal.openFile(fifo); }); });
    const bool settled = sharing.wait_for(std::chrono::seconds(2)) == std::future_status::ready;
    // Lets a share stuck opening the FIFO go on, so a regression fails here rather than hangs.
    const int writer = ::open(fifo.c_str(), O_WRONLY | O_NONBLOCK | O_CLOEXEC);

    if (writer >= 0) {
        ::close(writer);
    }

    EXPECT_TRUE(settled);
    EXPECT_EQ(sharing.get(), "cannot share " + fifo.string() + ": not a regular file");
    EXPECT_EQ(failureOf([&] { portal.openFile(file_.parent_path()); }),
              "cannot share " + file_.parent_path().string() + ": not a regular file");
    EXPECT_FALSE(fake.lastOpenFile().has_value());
    std::filesystem::remove(fifo);
}

/** The session's own bus, when the address it names has nobody listening, has no portal. */
TEST_F(OpenUriPortalTest, TheSessionBusWithNobodyAtItsAddressIsUnavailable) {
    const char* const previous = std::getenv("DBUS_SESSION_BUS_ADDRESS");
    const std::optional<std::string> saved = previous == nullptr ? std::nullopt : std::optional<std::string>(previous);

    ::setenv("DBUS_SESSION_BUS_ADDRESS", "unix:path=/nonexistent/rnl-open-uri-portal-test", 1);
    EXPECT_FALSE(OpenUriPortal().isAvailable());

    if (saved.has_value()) {
        ::setenv("DBUS_SESSION_BUS_ADDRESS", saved->c_str(), 1);
    } else {
        ::unsetenv("DBUS_SESSION_BUS_ADDRESS");
    }
}

} // namespace
