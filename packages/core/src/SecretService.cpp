#include "SecretService.h"

#include <cstdint>
#include <exception>
#include <iostream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
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
        throw std::runtime_error("Secret Service " + what + ": " + error.value.name);
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
};

int onPromptCompleted(sd_bus_message* message, void* userData, sd_bus_error* /*error*/) {
    auto& completion = *static_cast<PromptCompletion*>(userData);
    int dismissed = 1;

    sd_bus_message_read(message, "b", &dismissed);
    completion.completed = true;
    completion.dismissed = dismissed != 0;

    return 0;
}

} // namespace

void SecretServiceBackend::BusClose::operator()(sd_bus* bus) const noexcept { sd_bus_flush_close_unref(bus); }

void SecretServiceBackend::MessageUnref::operator()(sd_bus_message* message) const noexcept {
    sd_bus_message_unref(message);
}

SecretServiceBackend::SecretServiceBackend(const BusOpener& openBus, std::string applicationIdentifier)
    : applicationIdentifier_(std::move(applicationIdentifier)) {
    sd_bus* bus = nullptr;
    const int opened = openBus(&bus);

    bus_.reset(bus);
    requireBus(opened, "open the session bus");

    const Message session = call(kServicePath, kServiceInterface, "OpenSession", "sv", "plain", "s", "");

    requireBus(sd_bus_message_skip(session.get(), "v"), "OpenSession");
    session_ = readPath(session, "OpenSession");
    collection_ = readPath(call(kServicePath, kServiceInterface, "ReadAlias", "s", "default"), "ReadAlias");

    if (collection_ == kNoObject) {
        throw std::runtime_error("Secret Service has no default collection");
    }
}

std::unique_ptr<SecretBackend> SecretServiceBackend::connect(const BusOpener& openBus,
                                                             const std::string& applicationIdentifier) {
    try {
        return std::make_unique<SecretServiceBackend>(openBus, applicationIdentifier);
    } catch (const std::exception& error) {
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
    sd_bus_slot* slot = nullptr;

    requireBus(sd_bus_match_signal(bus_.get(), &slot, nullptr, prompt.c_str(), kPromptInterface, "Completed",
                                   onPromptCompleted, &completion),
               "watch the prompt");

    const std::unique_ptr<sd_bus_slot, SlotUnref> watch(slot);

    call(prompt, kPromptInterface, "Prompt", "s", "");

    while (!completion.completed) {
        requireBus(sd_bus_wait(bus_.get(), UINT64_MAX), "wait for the prompt");
        requireBus(sd_bus_process(bus_.get(), nullptr), "wait for the prompt");
    }

    if (completion.dismissed) {
        throw std::runtime_error("Secret Service: the keyring prompt was dismissed");
    }
}

std::unique_ptr<SecretBackend> connectSessionKeyring(const std::string& applicationIdentifier) {
    return SecretServiceBackend::connect(sd_bus_open_user, applicationIdentifier);
}

} // namespace react_native_linux
