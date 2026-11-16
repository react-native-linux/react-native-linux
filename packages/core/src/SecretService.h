#pragma once

#include "ExpoSecureStore.h"

#include <functional>
#include <memory>
#include <optional>
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
 * prompt, which this waits on until its `Completed` signal, so a call blocks for as long as the user takes.
 *
 * Threading contract: not thread-safe. `SecureStore` constructs it and calls it only from its one worker thread.
 */
class SecretServiceBackend final : public SecretBackend {
public:
    /** Fills its argument with a connected bus and answers its result, as `sd_bus_open_user` does. */
    using BusOpener = std::function<int(sd_bus**)>;

    /** Throws when `openBus` fails or its bus has no Secret Service with a default collection. */
    SecretServiceBackend(const BusOpener& openBus, std::string applicationIdentifier);

    std::optional<std::string> read(const SecretLocation& location) override;
    void write(const SecretLocation& location, const std::string& value) override;
    void remove(const SecretLocation& location) override;

    /** The backend, or null, with the reason on standard error, when the constructor throws. */
    static std::unique_ptr<SecretBackend> connect(const BusOpener& openBus, const std::string& applicationIdentifier);

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
    std::string session_;
    std::string collection_;
};

/** `SecureStore`'s keyring connector for this session: the Secret Service on the bus `sd_bus_open_user` opens. */
std::unique_ptr<SecretBackend> connectSessionKeyring(const std::string& applicationIdentifier);

} // namespace react_native_linux
