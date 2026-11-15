#pragma once

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <variant>
#include <vector>

struct sqlite3;
struct sqlite3_stmt;

namespace react_native_linux {

/** A bound parameter or a column value as JavaScript sees it: SQLite's INTEGER and REAL are both a number. */
using SQLiteValue = std::variant<std::nullptr_t, double, std::string>;
using SQLiteRow = std::vector<SQLiteValue>;

/**
 * What expo-sqlite's `normalizeParams` hands `NativeStatement.runAsync`: every parameter under a key, which is the
 * zero-based position when `byIndex` (upstream's `shouldPassAsArray`) and a parameter name such as `$id` otherwise.
 * A name the statement does not use is skipped, as upstream skips it.
 */
struct SQLiteBindParameters {
    std::vector<std::pair<std::string, SQLiteValue>> values;
    bool byIndex{true};
};

struct SQLiteRunResult {
    double lastInsertRowId{0};
    double changes{0};
    /** The first row the statement returned, or no values when it returned none. */
    SQLiteRow firstRowValues;
};

/**
 * The connections and prepared statements behind upstream expo-sqlite's `NativeDatabase` and `NativeStatement`
 * (#638), over the system SQLite, with the semantics of expo-sqlite's Android `NativeStatement.kt`: `run` resets the
 * statement, binds and steps once, `step` and `stepAll` continue from there, and closing a connection finalizes the
 * statements it still has. Every database is the file upstream's JavaScript names, under `directory`, which
 * `WindowSession` places at `$XDG_DATA_HOME/<app-id>/SQLite`, beside AsyncStorage's file, as expo-sqlite's default
 * directory is `<files>/SQLite`. A host that sets no directory answers none, so upstream's own error names it.
 *
 * Threading contract: `post` and `reserveHandle` may be called from any thread and never wait on SQLite. Every task
 * runs on the one worker thread this object owns, in the order it was posted, and must not throw. The methods from
 * `ensureDirectoryExists` down touch the connections, so only a posted task calls them, or a thread that posts
 * nothing. `setDirectory` runs before the bundle does, on the thread that then hands the runtime to the JavaScript
 * thread, which is the only one that reads `directory`.
 * The destructor runs every task already posted, so a queued write still lands, then joins the worker and closes
 * every connection still open. One worker serializes every connection, so a write on one connection while another
 * holds a transaction fails with SQLite's "database is locked" rather than waiting for a commit that the waiting
 * worker would never run.
 */
class SQLiteDatabases final {
public:
    using Task = std::function<void(SQLiteDatabases&)>;

    SQLiteDatabases();
    SQLiteDatabases(const SQLiteDatabases&) = delete;
    SQLiteDatabases(SQLiteDatabases&&) = delete;
    SQLiteDatabases& operator=(const SQLiteDatabases&) = delete;
    SQLiteDatabases& operator=(SQLiteDatabases&&) = delete;
    ~SQLiteDatabases() noexcept;

    void setDirectory(std::string directory);
    [[nodiscard]] const std::string& directory() const noexcept;
    void post(Task task);
    /**
     * A handle for a database or statement whose work is still queued: upstream constructs `NativeDatabase` and
     * `NativeStatement` synchronously and opens or prepares them afterwards, in `initAsync` and `prepareAsync`.
     */
    [[nodiscard]] int reserveHandle() noexcept;

    /** Creates the directory `databasePath` is in; `:memory:` has none. */
    static void ensureDirectoryExists(const std::string& databasePath);
    void open(int database, const std::string& databasePath);
    void close(int database);
    void exec(int database, const std::string& sql);
    [[nodiscard]] bool isInTransaction(int database);
    void prepare(int database, int statement, const std::string& sql);
    [[nodiscard]] SQLiteRunResult run(int statement, const SQLiteBindParameters& parameters);
    [[nodiscard]] std::optional<SQLiteRow> step(int statement);
    [[nodiscard]] std::vector<SQLiteRow> stepAll(int statement);
    void reset(int statement);
    [[nodiscard]] std::vector<std::string> columnNames(int statement);
    void finalize(int statement);

private:
    struct ConnectionCloser {
        void operator()(sqlite3* connection) const noexcept;
    };

    struct StatementFinalizer {
        void operator()(sqlite3_stmt* statement) const noexcept;
    };

    struct Statement {
        int database;
        std::unique_ptr<sqlite3_stmt, StatementFinalizer> prepared;
    };

    sqlite3& connection(int database);
    sqlite3_stmt& statement(int statement);
    void runTasks();

    std::string directory_;
    std::atomic<int> nextHandle_{1};
    std::map<int, std::unique_ptr<sqlite3, ConnectionCloser>> connections_;
    std::map<int, Statement> statements_;
    std::mutex mutex_;
    std::condition_variable wake_;
    std::deque<Task> tasks_;
    bool stopping_{false};
    std::thread worker_;
};

} // namespace react_native_linux
