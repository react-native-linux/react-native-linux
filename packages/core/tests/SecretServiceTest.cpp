#include "SecretService.h"

#include "ExpoSecureStore.h"

#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <functional>
#include <gtest/gtest.h>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <stop_token>
#include <string>
#include <sys/socket.h>
#include <systemd/sd-bus.h>
#include <thread>
#include <unistd.h>
#include <utility>

namespace {

using react_native_linux::SecretBackend;
using react_native_linux::SecretLocation;
using react_native_linux::SecretServiceBackend;
using react_native_linux::SecureStore;
using std::chrono::milliseconds;
using std::chrono::steady_clock;

constexpr char kCollection[] = "/org/freedesktop/secrets/collection/login";
constexpr char kPrompt[] = "/org/freedesktop/secrets/prompt/1";
constexpr char kSession[] = "/org/freedesktop/secrets/session/1";
constexpr char kApplication[] = "org.example.app";

const SecretLocation kToken{.service = "app", .key = "managed-relay-token"};

using Attributes = std::map<std::string, std::string>;

struct BusClose {
    void operator()(sd_bus* bus) const noexcept { sd_bus_flush_close_unref(bus); }
};

struct MessageUnref {
    void operator()(sd_bus_message* message) const noexcept { sd_bus_message_unref(message); }
};

using OwnedMessage = std::unique_ptr<sd_bus_message, MessageUnref>;

OwnedMessage newReply(sd_bus_message* call) {
    sd_bus_message* reply = nullptr;

    sd_bus_message_new_method_return(call, &reply);

    return OwnedMessage(reply);
}

Attributes readAttributes(sd_bus_message* message) {
    Attributes attributes;
    const char* name = nullptr;
    const char* value = nullptr;

    sd_bus_message_enter_container(message, 'a', "{ss}");

    while (sd_bus_message_read(message, "{ss}", &name, &value) > 0) {
        attributes[name] = value;
    }

    sd_bus_message_exit_container(message);

    return attributes;
}

/**
 * A Secret Service on the far end of a socket pair, served by sd-bus on its own thread: the methods
 * `SecretServiceBackend` calls, over one in-memory collection, with a lock and prompt, an injected failure and a
 * malformed reply to switch on. A peer connection needs no bus daemon, so the test runs wherever sd-bus does.
 */
class FakeSecretService {
public:
    struct Behavior {
        bool locked{false};
        bool dismissesPrompt{false};
        bool completesPrompt{true};
        bool leavesDuringPrompt{false};
        bool hasDefaultCollection{true};
        bool answersSessionMalformed{false};
        std::string failingMember;
        std::string failingError{SD_BUS_ERROR_FAILED};
        std::string silentMember;
    };

    explicit FakeSecretService(Behavior behavior) : behavior_(std::move(behavior)) {
        int descriptors[2] = {-1, -1};
        sd_id128_t identifier{};

        EXPECT_EQ(::socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, descriptors), 0);
        clientDescriptor_ = descriptors[1];
        sd_bus* server = nullptr;

        EXPECT_GE(sd_bus_new(&server), 0);
        server_.reset(server);
        EXPECT_GE(sd_bus_set_fd(server_.get(), descriptors[0], descriptors[0]), 0);
        EXPECT_GE(sd_id128_randomize(&identifier), 0);
        EXPECT_GE(sd_bus_set_server(server_.get(), 1, identifier), 0);
        EXPECT_GE(sd_bus_add_filter(server_.get(), nullptr, &FakeSecretService::onMessage, this), 0);
        EXPECT_GE(sd_bus_start(server_.get()), 0);
        thread_ = std::thread([this] {
            while (!stopping_.load()) {
                const int processed = sd_bus_process(server_.get(), nullptr);

                if (processed < 0) {
                    return;
                }

                if (processed == 0) {
                    sd_bus_wait(server_.get(), 10'000);
                }
            }
        });
    }

    FakeSecretService(const FakeSecretService&) = delete;
    FakeSecretService(FakeSecretService&&) = delete;
    FakeSecretService& operator=(const FakeSecretService&) = delete;
    FakeSecretService& operator=(FakeSecretService&&) = delete;

    ~FakeSecretService() {
        stopping_.store(true);
        thread_.join();
        ::close(clientDescriptor_);
    }

    /** Connects a client to this service, as `sd_bus_open_user` connects one to the session bus. */
    [[nodiscard]] SecretServiceBackend::BusOpener opener() const {
        return [descriptor = clientDescriptor_](sd_bus** bus) {
            const int client = ::dup(descriptor);

            sd_bus_new(bus);
            sd_bus_set_fd(*bus, client, client);

            return sd_bus_start(*bus);
        };
    }

    [[nodiscard]] std::size_t itemCount() {
        const std::lock_guard<std::mutex> lock(mutex_);

        return items_.size();
    }

    [[nodiscard]] int promptsShown() {
        const std::lock_guard<std::mutex> lock(mutex_);

        return promptsShown_;
    }

    [[nodiscard]] int promptsDismissed() {
        const std::lock_guard<std::mutex> lock(mutex_);

        return promptsDismissed_;
    }

    [[nodiscard]] std::string lastLabel() {
        const std::lock_guard<std::mutex> lock(mutex_);

        return lastLabel_;
    }

private:
    struct Item {
        Attributes attributes;
        std::string value;
    };

    static int onMessage(sd_bus_message* message, void* userData, sd_bus_error* /*error*/) {
        if (sd_bus_message_is_method_call(message, nullptr, nullptr) <= 0) {
            return 0;
        }

        static_cast<FakeSecretService*>(userData)->answer(message);

        return 1;
    }

    void answer(sd_bus_message* message) {
        const std::lock_guard<std::mutex> lock(mutex_);
        const std::string member = sd_bus_message_get_member(message);
        const std::string path = sd_bus_message_get_path(message);

        if (member == behavior_.silentMember) {
            return;
        }

        if (member == behavior_.failingMember) {
            sd_bus_reply_method_errorf(message, behavior_.failingError.c_str(), "injected failure");
        } else if (member == "OpenSession") {
            if (behavior_.answersSessionMalformed) {
                sd_bus_reply_method_return(message, "s", "not a variant");
            } else {
                sd_bus_reply_method_return(message, "vo", "s", "", kSession);
            }
        } else if (member == "ReadAlias") {
            sd_bus_reply_method_return(message, "o", behavior_.hasDefaultCollection ? kCollection : "/");
        } else if (member == "Unlock") {
            if (behavior_.locked) {
                sd_bus_reply_method_return(message, "aoo", 0, kPrompt);
            } else {
                sd_bus_reply_method_return(message, "aoo", 1, kCollection, "/");
            }
        } else if (member == "Prompt") {
            ++promptsShown_;
            sd_bus_reply_method_return(message, "");
            answerPrompt();
        } else if (member == "Dismiss") {
            ++promptsDismissed_;
        } else if (member == "SearchItems") {
            replyWithItems(message, readAttributes(message));
        } else if (member == "CreateItem") {
            createItem(message);
        } else if (member == "GetSecret") {
            replyWithSecret(message, items_.at(path).value);
        } else if (member == "Delete") {
            items_.erase(path);
            sd_bus_reply_method_return(message, "o", "/");
        }
    }

    void answerPrompt() {
        if (behavior_.leavesDuringPrompt) {
            sd_bus_message* building = nullptr;

            sd_bus_message_new_signal(server_.get(), &building, "/org/freedesktop/DBus", "org.freedesktop.DBus",
                                      "NameOwnerChanged");

            const OwnedMessage signal(building);

            sd_bus_message_set_sender(signal.get(), "org.freedesktop.DBus");
            sd_bus_message_append(signal.get(), "sss", "org.freedesktop.secrets", ":1.7", "");
            sd_bus_send(server_.get(), signal.get(), nullptr);
        } else if (behavior_.completesPrompt) {
            behavior_.locked = behavior_.dismissesPrompt;
            sd_bus_emit_signal(server_.get(), kPrompt, "org.freedesktop.Secret.Prompt", "Completed", "bv",
                               behavior_.dismissesPrompt, "ao", 0);
        }
    }

    void replyWithItems(sd_bus_message* message, const Attributes& attributes) {
        const OwnedMessage reply = newReply(message);

        sd_bus_message_open_container(reply.get(), 'a', "o");

        for (const auto& [itemPath, item] : items_) {
            if (item.attributes == attributes) {
                sd_bus_message_append(reply.get(), "o", itemPath.c_str());
            }
        }

        sd_bus_message_close_container(reply.get());
        sd_bus_send(server_.get(), reply.get(), nullptr);
    }

    void replyWithSecret(sd_bus_message* message, const std::string& value) {
        const OwnedMessage reply = newReply(message);

        sd_bus_message_open_container(reply.get(), 'r', "oayays");
        sd_bus_message_append(reply.get(), "o", kSession);
        sd_bus_message_append_array(reply.get(), 'y', nullptr, 0);
        sd_bus_message_append_array(reply.get(), 'y', value.data(), value.size());
        sd_bus_message_append(reply.get(), "s", "text/plain");
        sd_bus_message_close_container(reply.get());
        sd_bus_send(server_.get(), reply.get(), nullptr);
    }

    void createItem(sd_bus_message* message) {
        Item created;
        const char* name = nullptr;
        const void* bytes = nullptr;
        size_t size = 0;
        int replace = 0;

        sd_bus_message_enter_container(message, 'a', "{sv}");

        while (sd_bus_message_enter_container(message, 'e', "sv") > 0) {
            sd_bus_message_read(message, "s", &name);

            if (std::string(name) == "org.freedesktop.Secret.Item.Attributes") {
                sd_bus_message_enter_container(message, 'v', "a{ss}");
                created.attributes = readAttributes(message);
                sd_bus_message_exit_container(message);
            } else {
                const char* label = nullptr;

                sd_bus_message_read(message, "v", "s", &label);
                lastLabel_ = label;
            }

            sd_bus_message_exit_container(message);
        }

        sd_bus_message_exit_container(message);
        sd_bus_message_enter_container(message, 'r', "oayays");
        sd_bus_message_skip(message, "oay");
        sd_bus_message_read_array(message, 'y', &bytes, &size);
        created.value.assign(static_cast<const char*>(bytes), size);
        sd_bus_message_skip(message, "s");
        sd_bus_message_exit_container(message);
        sd_bus_message_read(message, "b", &replace);

        std::string itemPath = std::string(kCollection) + "/" + std::to_string(items_.size() + 1);

        for (const auto& [existingPath, item] : items_) {
            if (replace != 0 && item.attributes == created.attributes) {
                itemPath = existingPath;
            }
        }

        items_[itemPath] = std::move(created);
        sd_bus_reply_method_return(message, "oo", itemPath.c_str(), "/");
    }

    Behavior behavior_;
    std::mutex mutex_;
    std::map<std::string, Item> items_;
    int promptsShown_{0};
    int promptsDismissed_{0};
    std::string lastLabel_;
    std::unique_ptr<sd_bus, BusClose> server_;
    int clientDescriptor_{-1};
    std::atomic<bool> stopping_{false};
    std::thread thread_;
};

FakeSecretService::Behavior behaving(const std::function<void(FakeSecretService::Behavior&)>& change) {
    FakeSecretService::Behavior behavior;

    change(behavior);

    return behavior;
}

std::unique_ptr<SecretBackend>
connectTo(const FakeSecretService& service,
          std::chrono::microseconds promptTimeout = SecretServiceBackend::kPromptTimeout) {
    return SecretServiceBackend::connect(service.opener(), kApplication, std::stop_token(), promptTimeout);
}

template <typename Condition> bool eventually(Condition condition) {
    const auto deadline = steady_clock::now() + std::chrono::seconds(5);

    while (!condition() && steady_clock::now() < deadline) {
        std::this_thread::sleep_for(milliseconds(10));
    }

    return condition();
}

template <typename Work> std::string failureOf(Work work) {
    std::string failure = "no error";

    try {
        work();
    } catch (const std::runtime_error& error) {
        failure = error.what();
    }

    return failure;
}

/**
 * #270: a value goes into the default collection as an item keyed by application, service and key; a second write
 * replaces it rather than adding one, another service or key is another item, and a delete removes it.
 */
TEST(SecretServiceTest, AValueIsStoredReplacedAndDeletedAsOneItemOfTheDefaultCollection) {
    FakeSecretService service(behaving([](FakeSecretService::Behavior&) {}));
    const std::unique_ptr<SecretBackend> keyring = connectTo(service);
    const std::string binary = std::string("ünïcode") + '\0' + "bytes";

    ASSERT_NE(keyring, nullptr);
    EXPECT_EQ(keyring->read(kToken), std::nullopt);
    keyring->write(kToken, "first");
    keyring->write(kToken, binary);
    EXPECT_EQ(keyring->read(kToken), binary);
    EXPECT_EQ(service.itemCount(), 1U);
    EXPECT_EQ(service.lastLabel(), "managed-relay-token (org.example.app)");
    EXPECT_EQ(keyring->read({.service = "voice", .key = kToken.key}), std::nullopt);

    keyring->write({.service = "voice", .key = kToken.key}, "voice");
    keyring->remove(kToken);
    keyring->remove(kToken);
    EXPECT_EQ(keyring->read(kToken), std::nullopt);
    EXPECT_EQ(keyring->read({.service = "voice", .key = kToken.key}), "voice");
    EXPECT_EQ(service.promptsShown(), 0);
}

/** A locked collection is unlocked through the service's prompt, once; a dismissed prompt fails the call. */
TEST(SecretServiceTest, ALockedCollectionIsUnlockedThroughItsPromptAndADismissedPromptFails) {
    FakeSecretService locked(behaving([](FakeSecretService::Behavior& behavior) { behavior.locked = true; }));
    const std::unique_ptr<SecretBackend> keyring = connectTo(locked);

    keyring->write(kToken, "secret");
    EXPECT_EQ(keyring->read(kToken), "secret");
    EXPECT_EQ(locked.promptsShown(), 1);

    FakeSecretService dismissed(behaving([](FakeSecretService::Behavior& behavior) {
        behavior.locked = true;
        behavior.dismissesPrompt = true;
    }));

    EXPECT_EQ(failureOf([&] { connectTo(dismissed)->write(kToken, "secret"); }),
              "Secret Service: the keyring prompt was dismissed");
    EXPECT_EQ(dismissed.itemCount(), 0U);
}

/** A session with no bus, no owner of the Secret Service's name or no default collection has no keyring. */
TEST(SecretServiceTest, ThereIsNoKeyringWithoutABusAServiceOrADefaultCollection) {
    FakeSecretService withoutCollection(
        behaving([](FakeSecretService::Behavior& behavior) { behavior.hasDefaultCollection = false; }));
    FakeSecretService unknown(behaving([](FakeSecretService::Behavior& behavior) {
        behavior.failingMember = "OpenSession";
        behavior.failingError = SD_BUS_ERROR_SERVICE_UNKNOWN;
    }));
    FakeSecretService unowned(behaving([](FakeSecretService::Behavior& behavior) {
        behavior.failingMember = "OpenSession";
        behavior.failingError = SD_BUS_ERROR_NAME_HAS_NO_OWNER;
    }));

    EXPECT_EQ(SecretServiceBackend::connect([](sd_bus**) { return -ENOENT; }, kApplication, std::stop_token()),
              nullptr);
    EXPECT_EQ(connectTo(withoutCollection), nullptr);
    EXPECT_EQ(connectTo(unknown), nullptr);
    EXPECT_EQ(connectTo(unowned), nullptr);
}

/**
 * A Secret Service that is there but fails, answers nonsense or does not answer within the call timeout fails the
 * call rather than sending the value to the file fallback, so a slow keyring never splits the values between two
 * stores.
 */
TEST(SecretServiceTest, AKeyringThatFailsAnswersNonsenseOrStallsFailsTheCallInsteadOfFallingBack) {
    FakeSecretService failing(
        behaving([](FakeSecretService::Behavior& behavior) { behavior.failingMember = "OpenSession"; }));
    FakeSecretService malformed(
        behaving([](FakeSecretService::Behavior& behavior) { behavior.answersSessionMalformed = true; }));
    FakeSecretService stalled(
        behaving([](FakeSecretService::Behavior& behavior) { behavior.silentMember = "OpenSession"; }));

    EXPECT_EQ(failureOf([&] { static_cast<void>(connectTo(failing)); }),
              "Secret Service OpenSession: org.freedesktop.DBus.Error.Failed");
    EXPECT_EQ(failureOf([&] { static_cast<void>(connectTo(malformed)); }).rfind("Secret Service OpenSession: ", 0), 0U);

    const auto started = steady_clock::now();

    EXPECT_EQ(failureOf([&] {
                  static_cast<void>(connectTo(stalled, milliseconds(200)));
              }).rfind("Secret Service OpenSession: ", 0),
              0U);
    EXPECT_LT(steady_clock::now() - started, std::chrono::seconds(2));
}

/**
 * A prompt nobody answers gives up at the timeout: the call fails, and the service is told to dismiss the prompt
 * rather than leave it on screen for a call that already failed.
 */
TEST(SecretServiceTest, APromptNobodyAnswersIsDismissedAndFailsTheCallAtTheTimeout) {
    FakeSecretService unanswered(behaving([](FakeSecretService::Behavior& behavior) {
        behavior.locked = true;
        behavior.completesPrompt = false;
    }));
    const std::unique_ptr<SecretBackend> keyring = connectTo(unanswered, milliseconds(200));
    const auto started = steady_clock::now();

    EXPECT_EQ(failureOf([&] { keyring->write(kToken, "secret"); }), "Secret Service: the keyring prompt timed out");
    EXPECT_GE(steady_clock::now() - started, milliseconds(200));
    EXPECT_LT(steady_clock::now() - started, std::chrono::seconds(2));
    EXPECT_TRUE(eventually([&] { return unanswered.promptsDismissed() == 1; }));
    EXPECT_EQ(unanswered.itemCount(), 0U);
}

/** A service that leaves the bus while its prompt is up fails the call at once instead of at the timeout. */
TEST(SecretServiceTest, AServiceThatLeavesTheBusDuringItsPromptFailsTheCallAtOnce) {
    FakeSecretService leaving(behaving([](FakeSecretService::Behavior& behavior) {
        behavior.locked = true;
        behavior.leavesDuringPrompt = true;
    }));
    const std::unique_ptr<SecretBackend> keyring = connectTo(leaving);
    const auto started = steady_clock::now();

    EXPECT_EQ(failureOf([&] { keyring->write(kToken, "secret"); }),
              "Secret Service: the keyring service left the bus during its prompt");
    EXPECT_LT(steady_clock::now() - started, std::chrono::seconds(2));
}

/**
 * Tearing the store down while a prompt waits on the user ends the wait at once: the waiting call and the one queued
 * behind it both fail as cancelled, the second without showing a prompt of its own, and the destructor returns long
 * before the prompt's timeout.
 */
TEST(SecretServiceTest, TearingTheStoreDownCancelsAPromptStillWaitingOnTheUser) {
    FakeSecretService unanswered(behaving([](FakeSecretService::Behavior& behavior) {
        behavior.locked = true;
        behavior.completesPrompt = false;
    }));
    auto store = std::make_unique<SecureStore>();
    std::string firstFailure;
    std::string secondFailure;

    store->configure("/nonexistent/org.example.app", [&](const std::string& application, std::stop_token stopping) {
        return SecretServiceBackend::connect(unanswered.opener(), application, std::move(stopping));
    });
    store->post([&](SecureStore& posted) { firstFailure = failureOf([&] { posted.backend().write(kToken, "a"); }); });
    store->post([&](SecureStore& posted) { secondFailure = failureOf([&] { posted.backend().write(kToken, "b"); }); });
    ASSERT_TRUE(eventually([&] { return unanswered.promptsShown() == 1; }));

    const auto started = steady_clock::now();

    store.reset();
    EXPECT_LT(steady_clock::now() - started, std::chrono::seconds(2));
    EXPECT_EQ(firstFailure, "Secret Service: the keyring prompt was cancelled");
    EXPECT_EQ(secondFailure, "Secret Service: the keyring prompt was cancelled");
    EXPECT_EQ(unanswered.promptsShown(), 1);
    EXPECT_TRUE(eventually([&] { return unanswered.promptsDismissed() == 2; }));
}

/** A D-Bus error from the service reaches the caller with its name. */
TEST(SecretServiceTest, AnErrorFromTheServiceFailsTheCallWithItsName) {
    FakeSecretService failing(
        behaving([](FakeSecretService::Behavior& behavior) { behavior.failingMember = "SearchItems"; }));
    const std::unique_ptr<SecretBackend> keyring = connectTo(failing);

    EXPECT_EQ(failureOf([&] { static_cast<void>(keyring->read(kToken)); }),
              "Secret Service SearchItems: org.freedesktop.DBus.Error.Failed");
}

/** The session's own connector reaches no keyring when the session bus it names does not exist. */
TEST(SecretServiceTest, TheSessionConnectorAnswersNullWithoutASessionBus) {
    const char* const previous = std::getenv("DBUS_SESSION_BUS_ADDRESS");
    const std::optional<std::string> saved = previous == nullptr ? std::nullopt : std::optional<std::string>(previous);

    ::setenv("DBUS_SESSION_BUS_ADDRESS", "unix:path=/nonexistent/rnl-secret-service-test", 1);
    EXPECT_EQ(react_native_linux::connectSessionKeyring(kApplication, std::stop_token()), nullptr);

    if (saved.has_value()) {
        ::setenv("DBUS_SESSION_BUS_ADDRESS", saved->c_str(), 1);
    } else {
        ::unsetenv("DBUS_SESSION_BUS_ADDRESS");
    }
}

} // namespace
