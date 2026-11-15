#include "ExpoSQLite.h"

#include <cmath>
#include <filesystem>
#include <sqlite3.h>
#include <stdexcept>

namespace react_native_linux {

namespace {

constexpr double kInt64Limit = 0x1p63;
constexpr const char* kInMemoryDatabase = ":memory:";
constexpr int kPrimaryResultCodeMask = 0xff;

[[noreturn]] void fail(const std::string& message) { throw std::runtime_error("ExpoSQLite: " + message); }

void check(sqlite3* connection, int resultCode) {
    if (resultCode != SQLITE_OK) {
        fail(sqlite3_errmsg(connection));
    }
}

/** A whole number binds as an INTEGER, so `WHERE id = ?` matches the row a JavaScript number names. */
int bind(sqlite3_stmt* statement, int index, const SQLiteValue& value) {
    if (const double* number = std::get_if<double>(&value)) {
        if (std::trunc(*number) == *number && std::abs(*number) < kInt64Limit) {
            return sqlite3_bind_int64(statement, index, static_cast<sqlite3_int64>(*number));
        }

        return sqlite3_bind_double(statement, index, *number);
    }

    if (const std::string* text = std::get_if<std::string>(&value)) {
        return sqlite3_bind_text(statement, index, text->data(), static_cast<int>(text->size()), SQLITE_TRANSIENT);
    }

    return sqlite3_bind_null(statement, index);
}

SQLiteValue column(sqlite3_stmt* statement, int index) {
    switch (sqlite3_column_type(statement, index)) {
    case SQLITE_INTEGER:
        return static_cast<double>(sqlite3_column_int64(statement, index));
    case SQLITE_FLOAT:
        return sqlite3_column_double(statement, index);
    case SQLITE_TEXT:
        return std::string(reinterpret_cast<const char*>(sqlite3_column_text(statement, index)),
                           static_cast<size_t>(sqlite3_column_bytes(statement, index)));
    case SQLITE_BLOB:
        fail("BLOB columns are not supported on Linux");
    default:
        return nullptr;
    }
}

SQLiteRow row(sqlite3_stmt* statement) {
    SQLiteRow values;

    for (int index = 0; index < sqlite3_column_count(statement); ++index) {
        values.push_back(column(statement, index));
    }

    return values;
}

/**
 * Steps once: the row, or `nullopt` when the statement is done. A step that fails with SQLITE_BUSY leaves the
 * statement running, which fails every later COMMIT on the connection, so only that failure resets it: a reset
 * also clears any other error, which `finalize` must still report.
 */
std::optional<SQLiteRow> stepOnce(sqlite3_stmt* statement) {
    const int resultCode = sqlite3_step(statement);

    if (resultCode == SQLITE_ROW) {
        return row(statement);
    }

    if (resultCode != SQLITE_DONE) {
        const std::string message = sqlite3_errmsg(sqlite3_db_handle(statement));

        if ((resultCode & kPrimaryResultCodeMask) == SQLITE_BUSY) {
            sqlite3_reset(statement);
        }

        fail(message);
    }

    return std::nullopt;
}

} // namespace

void SQLiteDatabases::ConnectionCloser::operator()(sqlite3* connection) const noexcept { sqlite3_close(connection); }

void SQLiteDatabases::StatementFinalizer::operator()(sqlite3_stmt* statement) const noexcept {
    sqlite3_finalize(statement);
}

SQLiteDatabases::~SQLiteDatabases() noexcept = default;

void SQLiteDatabases::setDirectory(std::string directory) { directory_ = std::move(directory); }

const std::string& SQLiteDatabases::directory() const noexcept { return directory_; }

void SQLiteDatabases::post(Task task) {
    worker_.post([this, task = std::move(task)] { task(*this); });
}

int SQLiteDatabases::reserveHandle() noexcept { return nextHandle_.fetch_add(1); }

void SQLiteDatabases::ensureDirectoryExists(const std::string& databasePath) {
    const std::filesystem::path directory = std::filesystem::path(databasePath).parent_path();

    if (databasePath != kInMemoryDatabase && !directory.empty()) {
        std::filesystem::create_directories(directory);
    }
}

void SQLiteDatabases::open(int database, const std::string& databasePath) {
    sqlite3* opened = nullptr;
    const int resultCode = sqlite3_open(databasePath.c_str(), &opened);
    std::unique_ptr<sqlite3, ConnectionCloser> connection(opened);

    check(opened, resultCode);
    connections_.emplace(database, std::move(connection));
}

sqlite3& SQLiteDatabases::connection(int database) {
    const auto found = connections_.find(database);

    if (found == connections_.end()) {
        fail("the database is closed");
    }

    return *found->second;
}

sqlite3_stmt& SQLiteDatabases::statement(int statement) {
    const auto found = statements_.find(statement);

    if (found == statements_.end()) {
        fail("the statement is finalized");
    }

    return *found->second.prepared;
}

void SQLiteDatabases::close(int database) {
    static_cast<void>(connection(database));
    std::erase_if(statements_, [database](const auto& entry) { return entry.second.database == database; });
    connections_.erase(database);
}

void SQLiteDatabases::exec(int database, const std::string& sql) {
    sqlite3& connection = this->connection(database);

    check(&connection, sqlite3_exec(&connection, sql.c_str(), nullptr, nullptr, nullptr));
}

bool SQLiteDatabases::isInTransaction(int database) { return sqlite3_get_autocommit(&connection(database)) == 0; }

void SQLiteDatabases::prepare(int database, int statement, const std::string& sql) {
    sqlite3& connection = this->connection(database);
    sqlite3_stmt* prepared = nullptr;
    const int resultCode =
        sqlite3_prepare_v2(&connection, sql.c_str(), static_cast<int>(sql.size()), &prepared, nullptr);

    check(&connection, resultCode);

    if (prepared == nullptr) {
        fail("the statement has no SQL");
    }

    statements_.insert_or_assign(statement, Statement{database, {prepared, StatementFinalizer{}}});
}

SQLiteRunResult SQLiteDatabases::run(int statement, const SQLiteBindParameters& parameters) {
    sqlite3_stmt& prepared = this->statement(statement);
    sqlite3* connection = sqlite3_db_handle(&prepared);

    sqlite3_reset(&prepared);
    sqlite3_clear_bindings(&prepared);

    for (const auto& [key, value] : parameters.values) {
        const int index =
            parameters.byIndex ? std::stoi(key) + 1 : sqlite3_bind_parameter_index(&prepared, key.c_str());

        if (index > 0) {
            check(connection, bind(&prepared, index, value));
        }
    }

    SQLiteRunResult result;

    result.firstRowValues = stepOnce(&prepared).value_or(SQLiteRow{});
    result.lastInsertRowId = static_cast<double>(sqlite3_last_insert_rowid(connection));
    result.changes = static_cast<double>(sqlite3_changes(connection));

    return result;
}

std::optional<SQLiteRow> SQLiteDatabases::step(int statement) { return stepOnce(&this->statement(statement)); }

std::vector<SQLiteRow> SQLiteDatabases::stepAll(int statement) {
    sqlite3_stmt& prepared = this->statement(statement);
    std::vector<SQLiteRow> rows;

    for (std::optional<SQLiteRow> next = stepOnce(&prepared); next.has_value(); next = stepOnce(&prepared)) {
        rows.push_back(std::move(next.value()));
    }

    return rows;
}

void SQLiteDatabases::reset(int statement) {
    sqlite3_stmt& prepared = this->statement(statement);

    check(sqlite3_db_handle(&prepared), sqlite3_reset(&prepared));
}

std::vector<std::string> SQLiteDatabases::columnNames(int statement) {
    sqlite3_stmt& prepared = this->statement(statement);
    std::vector<std::string> names;

    for (int index = 0; index < sqlite3_column_count(&prepared); ++index) {
        names.emplace_back(sqlite3_column_name(&prepared, index));
    }

    return names;
}

/** SQLite destroys the statement even when it reports an earlier execution error, so the handle is gone either way. */
void SQLiteDatabases::finalize(int statement) {
    sqlite3_stmt* prepared = &this->statement(statement);
    sqlite3* connection = sqlite3_db_handle(prepared);
    auto node = statements_.extract(statement);

    check(connection, sqlite3_finalize(node.mapped().prepared.release()));
}

} // namespace react_native_linux
