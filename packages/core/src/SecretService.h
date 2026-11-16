#pragma once

#include "ExpoSecureStore.h"

#include <chrono>
#include <functional>
#include <memory>
#include <optional>
#include <stop_token>
#include <string>
#include <systemd/sd-bus.h>
#include <vector>

namespace react_native_linux {

/**
 * expo-secure-store's values in the desktop keyring (#270): the freedesktop Secret Service API
 * (`org.freedesktop.secrets`, which gnome-keyring and KWallet both serve) over the session bus, through sd-bus, the
 * D-Bus client `AppearancePortal` already links. Each value is an item of the default collection whose attributes
 * are `application`, `service` and `key`, so a second write replaces it. The session is the `plain` algorithm, as
 * libsecret falls back to: the value crosses the session bus, which only this user's processes can connect to,
 * unencrypted. Every call first unlocks the default collection; a locked one is unlocked through the service's own
 * prompt, which this waits on until its `Completed` signal. The wait gives up, dismisses the prompt and fails the call
 * when the prompt outlives `promptTimeout`, when the service leaves the bus, or when `stopping` is requested; every
 * method call gives up after `kCallTimeout`, or `promptTimeout` when that is shorter.
 *
 * Threading contract: not thread-safe. `SecureStore` constructs it and calls it only from its one worker thread;
 * `stopping` is the one thing another thread touches, and only through its `std::stop_source`.
 */
class SecretServiceBackend final : public SecretBackend {
public:
    /** Fills its argument with a connected bus and answers its result, as `sd_bus_open_user` does. */
    using BusOpener = std::function<int(sd_bus**)>;

    static constexpr std::chrono::microseconds kPromptTimeout = std::chrono::seconds(60);
    static constexpr std::chrono::microseconds kCallTimeout = std::chrono::seconds(10);

    /** Throws when `openBus` fails or its bus has no Secret Service with a default collection. */
    SecretServiceBackend(const BusOpener& openBus, std::string applicationIdentifier, std::stop_token stopping,
                         std::chrono::microseconds promptTimeout = kPromptTimeout);

    std::optional<std::string> read(const SecretLocation& location) override;
    void write(const SecretLocation& location, const std::string& value) override;
    void remove(const SecretLocation& location) override;

    /**
     * The backend, or null, with the reason on standard error, when the session has none: no session bus, no owner of
     * `org.freedesktop.secrets`, or no default collection. Any other failure throws, so a slow or failing keyring
     * fails the call rather than sending its values to another store.
     */
    static std::unique_ptr<SecretBackend> connect(const BusOpener& openBus, const std::string& applicationIdentifier,
                                                  std::stop_token stopping,
                                                  std::chrono::microseconds promptTimeout = kPromptTimeout);

private:
    struct BusClose {
        void operator()(sd_bus* bus) const noexcept;
    };

    struct MessageUnref {
        void operator()(sd_bus_message* message) const noexcept;
    };

    using Message = std::unique_ptr<sd_bus_message, MessageUnref>;

    template <typename... Arguments>
    Message call(const std::string& path, const char* interface, const char* member, const char* types,
                 Arguments... arguments);
    [[nodiscard]] std::string readPath(const Message& reply, const char* what) const;
    [[nodiscard]] std::vector<std::string> search(const SecretLocation& location);
    void unlockCollection();
    void settle(const std::string& prompt);

    std::unique_ptr<sd_bus, BusClose> bus_;
    std::string applicationIdentifier_;
    std::stop_token stopping_;
    std::chrono::microseconds promptTimeout_;
    std::string session_;
    std::string collection_;
};

/** `SecureStore`'s keyring connector for this session: the Secret Service on the bus `sd_bus_open_user` opens. */
std::unique_ptr<SecretBackend> connectSessionKeyring(const std::string& applicationIdentifier,
                                                     std::stop_token stopping);

} // namespace react_native_linux
