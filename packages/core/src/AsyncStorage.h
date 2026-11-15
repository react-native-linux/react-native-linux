#pragma once

#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

struct sqlite3;

namespace react_native_linux {

/**
 * The store behind `@react-native-async-storage/async-storage` (#23): every database an application names, as rows
 * of one SQLite table keyed by database name and key. One file per application, which `WindowSession` places at
 * `$XDG_DATA_HOME/<app-id>/async-storage.sqlite`; until something calls `setDatabasePath` the store is SQLite's
 * `:memory:`, so a headless run never writes into the user's home directory.
 *
 * `set` and `remove` are one transaction each, so a batch lands whole or not at all. A `nullopt` value in `set`
 * removes the key, which is what the module's `{ key, value: null }` entry means.
 *
 * Threading contract: the JavaScript thread only — every caller is a TurboModule method. `setDatabasePath` runs
 * before the bundle does, on the thread that then hands the runtime to the JavaScript thread.
 */
class KeyValueStore final {
public:
    using Entry = std::pair<std::string, std::optional<std::string>>;

    KeyValueStore();
    KeyValueStore(const KeyValueStore&) = delete;
    KeyValueStore(KeyValueStore&&) = delete;
    KeyValueStore& operator=(const KeyValueStore&) = delete;
    KeyValueStore& operator=(KeyValueStore&&) = delete;
    ~KeyValueStore() noexcept;

    /** Closes any open connection; the next call opens `databasePath`, creating its directory. */
    void setDatabasePath(std::string databasePath);

    [[nodiscard]] std::vector<Entry> get(const std::string& database, const std::vector<std::string>& keys);
    void set(const std::string& database, const std::vector<Entry>& entries);
    void remove(const std::string& database, const std::vector<std::string>& keys);
    [[nodiscard]] std::vector<std::string> keys(const std::string& database);
    void clear(const std::string& database);

private:
    struct ConnectionCloser {
        void operator()(sqlite3* connection) const noexcept;
    };

    sqlite3& connection();

    std::string databasePath_;
    std::unique_ptr<sqlite3, ConnectionCloser> connection_;
};

} // namespace react_native_linux
