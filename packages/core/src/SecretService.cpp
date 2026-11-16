#include "SecretService.h"

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <exception>
#include <iostream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace react_native_linux {

namespace {

constexpr char kDestination[] = "org.freedesktop.secrets";
constexpr char kServicePath[] = "/org/freedesktop/secrets";
constexpr char kServiceInterface[] = "org.freedesktop.Secret.Service";
constexpr char kCollectionInterface[] = "org.freedesktop.Secret.Collection";
constexpr char kItemInterface[] = "org.freedesktop.Secret.Item";
constexpr char kPromptInterface[] = "org.freedesktop.Secret.Prompt";
constexpr char kNoObject[] = "/";
constexpr char kServiceLeftMatch[] = "type='signal',sender='org.freedesktop.DBus',path='/org/freedesktop/DBus',"
                                     "interface='org.freedesktop.DBus',member='NameOwnerChanged',"
                                     "arg0='org.freedesktop.secrets',arg2=''";
constexpr std::chrono::milliseconds kStopCheckInterval(100);

/** The session has no Secret Service at all, which is the one failure `connect` answers with the file fallback. */
struct NoSecretService : std::runtime_error {
    using std::runtime_error::runtime_error;
};

/** An error a failed sd-bus call filled, or one `requireBus` fills from the call's result. */
struct BusError {
    BusError() = default;
    BusError(const BusError&) = delete;
    BusError(BusError&&) = delete;
    BusError& operator=(const BusError&) = delete;
    BusError& operator=(BusError&&) = delete;

    ~BusError() { sd_bus_error_free(&value); }

    sd_bus_error value{};
};

/** `result`, or a throw naming `what` and the D-Bus error, which `sd_bus_error_set_errno` keeps when already set. */
int requireBus(int result, const std::string& what, BusError& error) {
    if (result < 0) {
        sd_bus_error_set_errno(&error.value, result);

        const std::string failure = "Secret Service " + what + ": " + error.value.name;

        if (sd_bus_error_has_names(&error.value, SD_BUS_ERROR_SERVICE_UNKNOWN, SD_BUS_ERROR_NAME_HAS_NO_OWNER)) {
            throw NoSecretService(failure);
        }

        throw std::runtime_error(failure);
    }

    return result;
}

int requireBus(int result, const std::string& what) {
    BusError error;

    return requireBus(result, what, error);
}

struct SlotUnref {
    void operator()(sd_bus_slot* slot) const noexcept { sd_bus_slot_unref(slot); }
};

struct PromptCompletion {
    bool completed{false};
    bool dismissed{true};
    bool serviceLeft{false};
    std::string promptFailure;
};

/**
 * Dismisses the prompt unless it completed or its service left, on every way out of `settle`, so no prompt outlives its
 * call. The dismissal is a call bounded by the bus's call timeout, not a queued message, so closing the bus needs no
 * flush that a stalled peer could hold forever. It never auto-starts the service, so a service that left, whether or
 * not `settle` saw it go, is not started again only to dismiss a prompt it no longer has.
 */
struct PromptDismissal {
    PromptDismissal(sd_bus* bus, const std::string& prompt, const PromptCompletion& completion)
        : bus(bus), prompt(prompt), completion(completion) {}

    PromptDismissal(const PromptDismissal&) = delete;
    PromptDismissal(PromptDismissal&&) = delete;
    PromptDismissal& operator=(const PromptDismissal&) = delete;
    PromptDismissal& operator=(PromptDismissal&&) = delete;

    ~PromptDismissal() {
        if (!completion.completed && !completion.serviceLeft) {
            sd_bus_message* building = nullptr;

            sd_bus_message_new_method_call(bus, &building, kDestination, prompt.c_str(), kPromptInterface, "Dismiss");

            const std::unique_ptr<sd_bus_message, decltype(&sd_bus_message_unref)> dismissal(building,
                                                                                             sd_bus_message_unref);

            sd_bus_message_set_auto_start(dismissal.get(), 0);
            sd_bus_call(bus, dismissal.get(), 0, nullptr, nullptr);
        }
    }

    sd_bus* bus;
    const std::string& prompt;
    const PromptCompletion& completion;
};

/** `sd_bus_open_user`'s answers that mean the session has no bus: no address (`-ENOMEDIUM`), no socket, no listener. */
bool meansNoSessionBus(int error) { return error == -ENOMEDIUM || error == -ENOENT || error == -ECONNREFUSED; }

int onPromptCompleted(sd_bus_message* message, void* userData, sd_bus_error* /*error*/) {
    auto& completion = *static_cast<PromptCompletion*>(userData);
    int dismissed = 1;

    sd_bus_message_read(message, "b", &dismissed);
    completion.completed = true;
    completion.dismissed = dismissed != 0;

    return 0;
}

int onPromptReplied(sd_bus_message* message, void* userData, sd_bus_error* /*error*/) {
    const sd_bus_error* const failure = sd_bus_message_get_error(message);

    if (failure != nullptr) {
        static_cast<PromptCompletion*>(userData)->promptFailure = failure->name;
    }

    return 0;
}

int onServiceLeft(sd_bus_message* /*message*/, void* userData, sd_bus_error* /*error*/) {
    static_cast<PromptCompletion*>(userData)->serviceLeft = true;

    return 0;
}

} // namespace

void SecretServiceBackend::BusClose::operator()(sd_bus* bus) const noexcept { sd_bus_close_unref(bus); }

void SecretServiceBackend::MessageUnref::operator()(sd_bus_message* message) const noexcept {
    sd_bus_message_unref(message);
}

SecretServiceBackend::SecretServiceBackend(const BusOpener& openBus, std::string applicationIdentifier,
                                           std::stop_token stopping, std::chrono::microseconds promptTimeout)
    : applicationIdentifier_(std::move(applicationIdentifier)), stopping_(std::move(stopping)),
      promptTimeout_(promptTimeout) {
    sd_bus* bus = nullptr;
    const int opened = openBus(&bus);

    bus_.reset(bus);

    if (opened < 0) {
        const std::string reason = std::error_code(-opened, std::generic_category()).message();

        if (meansNoSessionBus(opened)) {
            throw NoSecretService("no session bus: " + reason);
        }

        throw std::runtime_error("Secret Service cannot open the session bus: " + reason);
    }

    requireBus(sd_bus_set_method_call_timeout(bus_.get(), std::min(promptTimeout, kCallTimeout).count()),
               "set the call timeout");

    const Message session = call(kServicePath, kServiceInterface, "OpenSession", "sv", "plain", "s", "");

    requireBus(sd_bus_message_skip(session.get(), "v"), "OpenSession");
    session_ = readPath(session, "OpenSession");
    collection_ = readPath(call(kServicePath, kServiceInterface, "ReadAlias", "s", "default"), "ReadAlias");

    if (collection_ == kNoObject) {
        throw NoSecretService("Secret Service has no default collection");
    }
}

std::unique_ptr<SecretBackend> SecretServiceBackend::connect(const BusOpener& openBus,
                                                             const std::string& applicationIdentifier,
                                                             std::stop_token stopping,
                                                             std::chrono::microseconds promptTimeout) {
    try {
        return std::make_unique<SecretServiceBackend>(openBus, applicationIdentifier, std::move(stopping),
                                                      promptTimeout);
    } catch (const NoSecretService& error) {
        std::cerr << "[rnl-secure-store] " << error.what() << std::endl;

        return nullptr;
    }
}

std::optional<std::string> SecretServiceBackend::read(const SecretLocation& location) {
    const std::vector<std::string> items = search(location);

    if (items.empty()) {
        return std::nullopt;
    }

    const Message reply = call(items.front(), kItemInterface, "GetSecret", "o", session_.c_str());
    const void* bytes = nullptr;
    size_t size = 0;

    requireBus(sd_bus_message_enter_container(reply.get(), 'r', "oayays"), "GetSecret");
    requireBus(sd_bus_message_skip(reply.get(), "oay"), "GetSecret");
    requireBus(sd_bus_message_read_array(reply.get(), 'y', &bytes, &size), "GetSecret");

    return std::string(static_cast<const char*>(bytes), size);
}

void SecretServiceBackend::write(const SecretLocation& location, const std::string& value) {
    unlockCollection();

    sd_bus_message* building = nullptr;

    requireBus(sd_bus_message_new_method_call(bus_.get(), &building, kDestination, collection_.c_str(),
                                              kCollectionInterface, "CreateItem"),
               "CreateItem");

    const Message request(building);
    const std::string label = location.key + " (" + applicationIdentifier_ + ")";

    requireBus(sd_bus_message_append(request.get(), "a{sv}", 2, "org.freedesktop.Secret.Item.Label", "s", label.c_str(),
                                     "org.freedesktop.Secret.Item.Attributes", "a{ss}", 3, "application",
                                     applicationIdentifier_.c_str(), "service", location.service.c_str(), "key",
                                     location.key.c_str()),
               "CreateItem");
    requireBus(sd_bus_message_open_container(request.get(), 'r', "oayays"), "CreateItem");
    requireBus(sd_bus_message_append(request.get(), "o", session_.c_str()), "CreateItem");
    requireBus(sd_bus_message_append_array(request.get(), 'y', nullptr, 0), "CreateItem");
    requireBus(sd_bus_message_append_array(request.get(), 'y', value.data(), value.size()), "CreateItem");
    requireBus(sd_bus_message_append(request.get(), "s", "text/plain"), "CreateItem");
    requireBus(sd_bus_message_close_container(request.get()), "CreateItem");
    requireBus(sd_bus_message_append(request.get(), "b", 1), "CreateItem");

    BusError error;
    sd_bus_message* reply = nullptr;

    requireBus(sd_bus_call(bus_.get(), request.get(), 0, &error.value, &reply), "CreateItem", error);

    const Message created(reply);

    requireBus(sd_bus_message_skip(created.get(), "o"), "CreateItem");
    settle(readPath(created, "CreateItem"));
}

void SecretServiceBackend::remove(const SecretLocation& location) {
    for (const std::string& item : search(location)) {
        settle(readPath(call(item, kItemInterface, "Delete", ""), "Delete"));
    }
}

template <typename... Arguments>
SecretServiceBackend::Message SecretServiceBackend::call(const std::string& path, const char* interface,
                                                         const char* member, const char* types,
                                                         Arguments... arguments) {
    BusError error;
    sd_bus_message* reply = nullptr;

    requireBus(sd_bus_call_method(bus_.get(), kDestination, path.c_str(), interface, member, &error.value, &reply,
                                  types, arguments...),
               member, error);

    return Message(reply);
}

std::string SecretServiceBackend::readPath(const Message& reply, const char* what) const {
    const char* path = nullptr;

    requireBus(sd_bus_message_read(reply.get(), "o", &path), what);

    return path;
}

std::vector<std::string> SecretServiceBackend::search(const SecretLocation& location) {
    unlockCollection();

    const Message reply =
        call(collection_, kCollectionInterface, "SearchItems", "a{ss}", 3, "application",
             applicationIdentifier_.c_str(), "service", location.service.c_str(), "key", location.key.c_str());
    std::vector<std::string> items;
    const char* item = nullptr;

    requireBus(sd_bus_message_enter_container(reply.get(), 'a', "o"), "SearchItems");

    while (requireBus(sd_bus_message_read(reply.get(), "o", &item), "SearchItems") > 0) {
        items.emplace_back(item);
    }

    return items;
}

void SecretServiceBackend::unlockCollection() {
    const Message reply = call(kServicePath, kServiceInterface, "Unlock", "ao", 1, collection_.c_str());

    requireBus(sd_bus_message_skip(reply.get(), "ao"), "Unlock");
    settle(readPath(reply, "Unlock"));
}

void SecretServiceBackend::settle(const std::string& prompt) {
    if (prompt == kNoObject) {
        return;
    }

    PromptCompletion completion;
    sd_bus_slot* completedSlot = nullptr;
    sd_bus_slot* serviceSlot = nullptr;

    requireBus(sd_bus_match_signal(bus_.get(), &completedSlot, nullptr, prompt.c_str(), kPromptInterface, "Completed",
                                   onPromptCompleted, &completion),
               "watch the prompt");

    const std::unique_ptr<sd_bus_slot, SlotUnref> completedWatch(completedSlot);

    requireBus(sd_bus_add_match(bus_.get(), &serviceSlot, kServiceLeftMatch, onServiceLeft, &completion),
               "watch the service");

    const std::unique_ptr<sd_bus_slot, SlotUnref> serviceWatch(serviceSlot);
    const PromptDismissal dismissal(bus_.get(), prompt, completion);
    const auto deadline = std::chrono::steady_clock::now() + promptTimeout_;
    sd_bus_slot* promptSlot = nullptr;

    if (!stopping_.stop_requested()) {
        requireBus(sd_bus_call_method_async(bus_.get(), &promptSlot, kDestination, prompt.c_str(), kPromptInterface,
                                            "Prompt", onPromptReplied, &completion, "s", ""),
                   "Prompt");
    }

    const std::unique_ptr<sd_bus_slot, SlotUnref> promptCall(promptSlot);

    while (!completion.completed && !completion.serviceLeft && completion.promptFailure.empty() &&
           !stopping_.stop_requested()) {
        const std::chrono::steady_clock::duration left = deadline - std::chrono::steady_clock::now();

        if (left <= std::chrono::steady_clock::duration::zero()) {
            break;
        }

        if (requireBus(sd_bus_process(bus_.get(), nullptr), "wait for the prompt") == 0) {
            const auto slice = std::min<std::chrono::steady_clock::duration>(left, kStopCheckInterval);

            requireBus(sd_bus_wait(bus_.get(), std::chrono::duration_cast<std::chrono::microseconds>(slice).count()),
                       "wait for the prompt");
        }
    }

    if (completion.completed) {
        if (completion.dismissed) {
            throw std::runtime_error("Secret Service: the keyring prompt was dismissed");
        }

        return;
    }

    if (!completion.promptFailure.empty()) {
        throw std::runtime_error("Secret Service Prompt: " + completion.promptFailure);
    }

    if (completion.serviceLeft) {
        throw std::runtime_error("Secret Service: the keyring service left the bus during its prompt");
    }

    throw std::runtime_error(stopping_.stop_requested() ? "Secret Service: the keyring prompt was cancelled"
                                                        : "Secret Service: the keyring prompt timed out");
}

std::unique_ptr<SecretBackend> connectSessionKeyring(const std::string& applicationIdentifier,
                                                     std::stop_token stopping) {
    return SecretServiceBackend::connect(sd_bus_open_user, applicationIdentifier, std::move(stopping));
}

} // namespace react_native_linux
