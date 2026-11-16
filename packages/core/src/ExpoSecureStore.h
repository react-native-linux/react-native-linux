#pragma once

#include "SerialWorker.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <stop_token>
#include <string>
#include <unistd.h>
#include <vector>

namespace facebook::react {
class CallInvoker;
class TurboModule;
} // namespace facebook::react

namespace react_native_linux {

/** Where one expo-secure-store value lives: upstream's `keychainService` option and the key. */
struct SecretLocation {
    std::string service;
    std::string key;
};

/** A store for expo-secure-store's values: the desktop keyring, or the encrypted file when there is none. */
class SecretBackend {
public:
    SecretBackend() = default;
    SecretBackend(const SecretBackend&) = delete;
    SecretBackend(SecretBackend&&) = delete;
    SecretBackend& operator=(const SecretBackend&) = delete;
    SecretBackend& operator=(SecretBackend&&) = delete;
    virtual ~SecretBackend() = default;

    [[nodiscard]] virtual std::optional<std::string> read(const SecretLocation& location) = 0;
    virtual void write(const SecretLocation& location, const std::string& value) = 0;
    virtual void remove(const SecretLocation& location) = 0;
};

/**
 * The fallback for a session with no Secret Service (#270): one file per value under `directory`, mode 0600 in a
 * 0700 directory, sealed with AES-256-GCM under a key HKDF-SHA256 derives from the machine id, the user id and the
 * application id, with a fresh random nonce per write and the location as associated data. A write lands through a
 * temporary file of its own and a rename, so a reader never sees half of one and two writers never share one; the
 * directory is synced after every rename and delete, so a crash keeps either the old value or the new one, and the
 * parent of every directory a write creates is synced before the value lands, again on the next write when that sync
 * failed.
 *
 * Every input to the key is readable by any process of the same user, so this protects a copied file on another
 * machine or account, not a value against code running as this user. The key is wiped when the backend is
 * destroyed.
 */
class EncryptedFileBackend final : public SecretBackend {
public:
    /** `fsync`, which a test replaces to watch or fail the syncs. */
    using Sync = std::function<int(int descriptor)>;

    EncryptedFileBackend(const std::filesystem::path& directory, const std::filesystem::path& machineIdentifierPath,
                         const std::string& applicationIdentifier, Sync sync = ::fsync);
    EncryptedFileBackend(const EncryptedFileBackend&) = delete;
    EncryptedFileBackend(EncryptedFileBackend&&) = delete;
    EncryptedFileBackend& operator=(const EncryptedFileBackend&) = delete;
    EncryptedFileBackend& operator=(EncryptedFileBackend&&) = delete;
    ~EncryptedFileBackend() override;

    [[nodiscard]] std::optional<std::string> read(const SecretLocation& location) override;
    void write(const SecretLocation& location, const std::string& value) override;
    void remove(const SecretLocation& location) override;

private:
    [[nodiscard]] std::filesystem::path pathOf(const SecretLocation& location) const;
    void createDirectory();

    std::filesystem::path directory_;
    std::array<std::uint8_t, 32> key_{};
    Sync sync_;
    std::vector<std::filesystem::path> unsyncedDirectories_;
};

/**
 * What `ExpoSecureStore` runs over: the backend, chosen once on first use, and the one thread every backend call
 * runs on. The keyring wins whenever `connectKeyring` reaches a Secret Service; when it answers that the session has
 * none, the values go to an `EncryptedFileBackend` under `<application directory>/SecureStore`, and a line on
 * standard error says so. When it throws, the call fails and the next one asks again.
 *
 * Threading contract: `configure` runs before the bundle does, on the thread that then hands the runtime to the
 * JavaScript thread. `post` may be called from any thread and never waits on the backend. Every task runs on the
 * `SerialWorker` this object owns, in the order it was posted, and must not throw; only a task calls `backend`, so
 * neither the D-Bus connection nor the files are ever touched from two threads. The destructor first requests the
 * stop the connector's token carries, which ends a keyring prompt still waiting on the user, then runs every task
 * already posted, so a queued write still lands.
 */
class SecureStore final {
public:
    using Task = std::function<void(SecureStore&)>;
    /** The Secret Service for an application id, or null when the session has none; `stopping` ends its waits. */
    using KeyringConnector = std::function<std::unique_ptr<SecretBackend>(const std::string& applicationIdentifier,
                                                                          std::stop_token stopping)>;

    SecureStore() = default;
    SecureStore(const SecureStore&) = delete;
    SecureStore(SecureStore&&) = delete;
    SecureStore& operator=(const SecureStore&) = delete;
    SecureStore& operator=(SecureStore&&) = delete;
    ~SecureStore() noexcept;

    /** The application id is `applicationDirectory`'s last component, as `WindowSession` names it. */
    void configure(std::filesystem::path applicationDirectory, KeyringConnector connectKeyring,
                   std::filesystem::path machineIdentifierPath = "/etc/machine-id");
    void post(Task task);
    [[nodiscard]] SecretBackend& backend();

private:
    std::filesystem::path applicationDirectory_;
    KeyringConnector connectKeyring_ = [](const std::string&, const std::stop_token&) {
        return std::unique_ptr<SecretBackend>();
    };
    std::filesystem::path machineIdentifierPath_;
    std::unique_ptr<SecretBackend> backend_;
    std::stop_source stopping_;
    // Last, so it is destroyed first: its queued tasks still use the backend.
    SerialWorker worker_;
};

/** The `ExpoSecureStore` TurboModule upstream expo-secure-store's JavaScript requires, over `store`, which must outlive
 * it. */
std::shared_ptr<facebook::react::TurboModule>
makeExpoSecureStoreModule(std::shared_ptr<facebook::react::CallInvoker> jsInvoker, SecureStore& store);

} // namespace react_native_linux
