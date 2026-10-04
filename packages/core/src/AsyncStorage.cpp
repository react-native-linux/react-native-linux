#include "AsyncStorage.h"

#include <filesystem>
#include <sqlite3.h>
#include <stdexcept>

namespace react_native_linux {

namespace {

constexpr const char* kInMemoryDatabase = ":memory:";
constexpr const char* kSchema =
    "CREATE TABLE IF NOT EXISTS entries (database_name TEXT NOT NULL, entry_key TEXT NOT NULL, "
    "entry_value TEXT NOT NULL, PRIMARY KEY (database_name, entry_key)) WITHOUT ROWID";

int check(sqlite3& connection, int resultCode) {
    if (resultCode != SQLITE_OK && resultCode != SQLITE_ROW && resultCode != SQLITE_DONE) {
        throw std::runtime_error(std::string("AsyncStorage: ") + sqlite3_errmsg(&connection));
    }

    return resultCode;
}

class Statement final {
public:
    Statement(sqlite3& connection, const char* sql) : connection_(connection) {
        check(connection_, sqlite3_prepare_v2(&connection_, sql, -1, &statement_, nullptr));
    }

    Statement(const Statement&) = delete;
    Statement(Statement&&) = delete;
    Statement& operator=(const Statement&) = delete;
    Statement& operator=(Statement&&) = delete;

    ~Statement() noexcept { sqlite3_finalize(statement_); }

    Statement& bind(int index, const std::string& text) {
        check(connection_,
              sqlite3_bind_text(statement_, index, text.data(), static_cast<int>(text.size()), SQLITE_TRANSIENT));

        return *this;
    }

    bool step() { return check(connection_, sqlite3_step(statement_)) == SQLITE_ROW; }

    void reset() { sqlite3_reset(statement_); }

    void run() {
        step();
        reset();
    }

    std::string column() const {
        return {reinterpret_cast<const char*>(sqlite3_column_text(statement_, 0)),
                static_cast<size_t>(sqlite3_column_bytes(statement_, 0))};
    }

private:
    sqlite3& connection_;
    sqlite3_stmt* statement_ = nullptr;
};

/** Rolls back unless `commit` ran, so an exception anywhere in a batch leaves the database as it was. */
class Transaction final {
public:
    explicit Transaction(sqlite3& connection) : connection_(connection) {
        check(connection_, sqlite3_exec(&connection_, "BEGIN IMMEDIATE", nullptr, nullptr, nullptr));
    }

    Transaction(const Transaction&) = delete;
    Transaction(Transaction&&) = delete;
    Transaction& operator=(const Transaction&) = delete;
    Transaction& operator=(Transaction&&) = delete;

    ~Transaction() noexcept {
        if (!committed_) {
            sqlite3_exec(&connection_, "ROLLBACK", nullptr, nullptr, nullptr);
        }
    }

    void commit() {
        check(connection_, sqlite3_exec(&connection_, "COMMIT", nullptr, nullptr, nullptr));
        committed_ = true;
    }

private:
    sqlite3& connection_;
    bool committed_ = false;
};

} // namespace

void KeyValueStore::ConnectionCloser::operator()(sqlite3* connection) const noexcept { sqlite3_close(connection); }

KeyValueStore::KeyValueStore() : databasePath_(kInMemoryDatabase) {}

KeyValueStore::~KeyValueStore() noexcept = default;

void KeyValueStore::setDatabasePath(std::string databasePath) {
    connection_.reset();
    databasePath_ = std::move(databasePath);
}

sqlite3& KeyValueStore::connection() {
    if (connection_ != nullptr) {
        return *connection_;
    }

    const std::filesystem::path directory = std::filesystem::path(databasePath_).parent_path();

    if (!directory.empty()) {
        std::filesystem::create_directories(directory);
    }

    sqlite3* opened = nullptr;
    const int resultCode = sqlite3_open(databasePath_.c_str(), &opened);
    std::unique_ptr<sqlite3, ConnectionCloser> connection(opened);

    check(*connection, resultCode);
    check(*connection, sqlite3_exec(connection.get(), kSchema, nullptr, nullptr, nullptr));
    connection_ = std::move(connection);

    return *connection_;
}

std::vector<KeyValueStore::Entry> KeyValueStore::get(const std::string& database,
                                                     const std::vector<std::string>& keys) {
    Statement select(connection(), "SELECT entry_value FROM entries WHERE database_name = ?1 AND entry_key = ?2");
    std::vector<Entry> entries;

    select.bind(1, database);

    for (const std::string& key : keys) {
        std::optional<std::string> value;

        if (select.bind(2, key).step()) {
            value = select.column();
        }

        select.reset();
        entries.emplace_back(key, std::move(value));
    }

    return entries;
}

void KeyValueStore::set(const std::string& database, const std::vector<Entry>& entries) {
    Transaction transaction(connection());
    Statement upsert(connection(), "INSERT OR REPLACE INTO entries VALUES (?1, ?2, ?3)");
    Statement erase(connection(), "DELETE FROM entries WHERE database_name = ?1 AND entry_key = ?2");

    for (const auto& [key, value] : entries) {
        if (value.has_value()) {
            upsert.bind(1, database).bind(2, key).bind(3, value.value()).run();
        } else {
            erase.bind(1, database).bind(2, key).run();
        }
    }

    transaction.commit();
}

void KeyValueStore::remove(const std::string& database, const std::vector<std::string>& keys) {
    Transaction transaction(connection());
    Statement erase(connection(), "DELETE FROM entries WHERE database_name = ?1 AND entry_key = ?2");

    for (const std::string& key : keys) {
        erase.bind(1, database).bind(2, key).run();
    }

    transaction.commit();
}

std::vector<std::string> KeyValueStore::keys(const std::string& database) {
    Statement select(connection(), "SELECT entry_key FROM entries WHERE database_name = ?1");
    std::vector<std::string> keys;

    select.bind(1, database);

    while (select.step()) {
        keys.push_back(select.column());
    }

    return keys;
}

void KeyValueStore::clear(const std::string& database) {
    Statement(connection(), "DELETE FROM entries WHERE database_name = ?1").bind(1, database).run();
}

} // namespace react_native_linux
