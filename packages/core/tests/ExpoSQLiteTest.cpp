#include "ExpoSQLite.h"

#include <filesystem>
#include <future>
#include <gtest/gtest.h>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {

using react_native_linux::SQLiteBindParameters;
using react_native_linux::SQLiteDatabases;
using react_native_linux::SQLiteRow;
using react_native_linux::SQLiteRunResult;
using react_native_linux::SQLiteValue;

namespace fs = std::filesystem;

using Rows = std::vector<SQLiteRow>;
using namespace std::string_literals;

/** Each case owns a directory named after it, gone before and after the case runs. */
class ExpoSQLiteTest : public ::testing::Test {
protected:
    ExpoSQLiteTest() { fs::remove_all(root_); }

    ExpoSQLiteTest(const ExpoSQLiteTest&) = delete;
    ExpoSQLiteTest(ExpoSQLiteTest&&) = delete;
    ExpoSQLiteTest& operator=(const ExpoSQLiteTest&) = delete;
    ExpoSQLiteTest& operator=(ExpoSQLiteTest&&) = delete;

    ~ExpoSQLiteTest() override { fs::remove_all(root_); }

    std::string directory() const { return (root_ / "SQLite").string(); }

    int open(const std::string& name) {
        const std::string path = (fs::path(directory()) / name).string();
        const int database = databases_.reserveHandle();

        SQLiteDatabases::ensureDirectoryExists(path);
        databases_.open(database, path);

        return database;
    }

    int prepare(int database, const std::string& sql) {
        const int statement = databases_.reserveHandle();

        databases_.prepare(database, statement, sql);

        return statement;
    }

    template <typename Call> static std::string errorOf(Call&& call) {
        try {
            call();
        } catch (const std::exception& error) {
            return error.what();
        }

        return "no error";
    }

    SQLiteDatabases databases_;

private:
    const fs::path root_ =
        fs::temp_directory_path() /
        (std::string("rnl-expo-sqlite-") + ::testing::UnitTest::GetInstance()->current_test_info()->name());
};

TEST_F(ExpoSQLiteTest, RunBindsByPositionAndAnswersTheFirstRowThenStepAndStepAllContinueTheCursor) {
    const int database = open("cursor.db");

    databases_.exec(database, "CREATE TABLE entries (id INTEGER PRIMARY KEY, label TEXT, ratio REAL, note TEXT)");

    const int insert = prepare(database, "INSERT INTO entries (label, ratio, note) VALUES (?, ?, ?)");

    for (const std::string& label : {"a"s, "b"s, "c"s}) {
        static_cast<void>(databases_.run(insert, {{{"0", label}, {"1", 0.5}, {"2", nullptr}}}));
    }

    const int select = prepare(database, "SELECT id, label, ratio, note FROM entries ORDER BY id");

    EXPECT_EQ(databases_.columnNames(select), (std::vector<std::string>{"id", "label", "ratio", "note"}));
    EXPECT_EQ(databases_.run(select, {}).firstRowValues, (SQLiteRow{1.0, "a"s, 0.5, nullptr}));
    EXPECT_EQ(databases_.step(select), (SQLiteRow{2.0, "b"s, 0.5, nullptr}));
    EXPECT_EQ(databases_.stepAll(select), (Rows{{3.0, "c"s, 0.5, nullptr}}));

    databases_.reset(select);

    EXPECT_EQ(databases_.step(select), (SQLiteRow{1.0, "a"s, 0.5, nullptr}));
    EXPECT_EQ(databases_.run(select, {}).firstRowValues, (SQLiteRow{1.0, "a"s, 0.5, nullptr}));

    databases_.finalize(select);
    databases_.finalize(insert);
}

TEST_F(ExpoSQLiteTest, AWriteAnswersNoRowWithItsRowIdAndChangeCount) {
    const int database = open("write.db");

    databases_.exec(database, "CREATE TABLE entries (value TEXT)");

    const SQLiteRunResult inserted =
        databases_.run(prepare(database, "INSERT INTO entries VALUES (?), (?)"), {{{"0", "x"s}, {"1", "y"s}}});

    EXPECT_EQ(inserted.lastInsertRowId, 2);
    EXPECT_EQ(inserted.changes, 2);
    EXPECT_TRUE(inserted.firstRowValues.empty());
}

TEST_F(ExpoSQLiteTest, NamedParametersBindByNameAndANameTheStatementDoesNotUseIsSkipped) {
    const int database = open("named.db");
    const SQLiteBindParameters parameters{{{"$number", 1.0}, {":text", "x"s}, {"$unused", 2.0}}, false};

    EXPECT_EQ(databases_.run(prepare(database, "SELECT $number, :text"), parameters).firstRowValues,
              (SQLiteRow{1.0, "x"s}));
}

TEST_F(ExpoSQLiteTest, AWholeNumberBindsAsAnIntegerAndAnyOtherNumberAsAReal) {
    const int database = open("numbers.db");

    EXPECT_EQ(
        databases_
            .run(prepare(database, "SELECT typeof(?), typeof(?), typeof(?)"), {{{"0", 42.0}, {"1", 1.5}, {"2", 1e300}}})
            .firstRowValues,
        (SQLiteRow{"integer"s, "real"s, "real"s}));
}

TEST_F(ExpoSQLiteTest, ErrorsCarrySQLitesMessageAndAFailedRunIsReportedAgainByResetAndFinalize) {
    const int database = open("errors.db");

    databases_.exec(database, "CREATE TABLE unique_entries (value TEXT UNIQUE)");

    const int insert = prepare(database, "INSERT INTO unique_entries VALUES (?)");

    static_cast<void>(databases_.run(insert, {{{"0", "a"s}}}));

    EXPECT_EQ(errorOf([&] { static_cast<void>(databases_.run(insert, {{{"0", "a"s}}})); }),
              "ExpoSQLite: UNIQUE constraint failed: unique_entries.value");
    EXPECT_EQ(errorOf([&] { databases_.reset(insert); }), "ExpoSQLite: UNIQUE constraint failed: unique_entries.value");
    EXPECT_EQ(errorOf([&] { static_cast<void>(databases_.run(insert, {{{"0", "a"s}}})); }),
              "ExpoSQLite: UNIQUE constraint failed: unique_entries.value");
    EXPECT_EQ(errorOf([&] { databases_.finalize(insert); }),
              "ExpoSQLite: UNIQUE constraint failed: unique_entries.value");
    EXPECT_EQ(errorOf([&] { static_cast<void>(databases_.step(insert)); }), "ExpoSQLite: the statement is finalized");
    EXPECT_EQ(errorOf([&] { databases_.exec(database, "NOT SQL"); }), "ExpoSQLite: near \"NOT\": syntax error");
    EXPECT_EQ(errorOf([&] { static_cast<void>(prepare(database, "SELECT FROM")); }),
              "ExpoSQLite: near \"FROM\": syntax error");
    EXPECT_EQ(errorOf([&] { static_cast<void>(prepare(database, " ")); }), "ExpoSQLite: the statement has no SQL");
    EXPECT_EQ(errorOf([&] { static_cast<void>(databases_.run(prepare(database, "SELECT ?"), {{{"1", 2.0}}})); }),
              "ExpoSQLite: column index out of range");
    EXPECT_EQ(errorOf([&] { static_cast<void>(databases_.run(prepare(database, "SELECT x'00ff'"), {})); }),
              "ExpoSQLite: BLOB columns are not supported on Linux");
}

TEST_F(ExpoSQLiteTest, ClosingFinalizesTheStatementsOfThatConnectionOnly) {
    const int database = open("closed.db");
    const int other = open("other.db");
    const int statement = prepare(database, "SELECT 1");
    const int otherStatement = prepare(other, "SELECT 2");

    databases_.close(database);

    EXPECT_EQ(errorOf([&] { static_cast<void>(databases_.run(statement, {})); }),
              "ExpoSQLite: the statement is finalized");
    EXPECT_EQ(errorOf([&] { databases_.exec(database, "SELECT 1"); }), "ExpoSQLite: the database is closed");
    EXPECT_EQ(errorOf([&] { databases_.close(database); }), "ExpoSQLite: the database is closed");
    EXPECT_EQ(databases_.run(otherStatement, {}).firstRowValues, (SQLiteRow{2.0}));
}

TEST_F(ExpoSQLiteTest, IsInTransactionFollowsBeginAndCommit) {
    const int database = open("transaction.db");

    EXPECT_FALSE(databases_.isInTransaction(database));
    databases_.exec(database, "BEGIN");
    EXPECT_TRUE(databases_.isInTransaction(database));
    databases_.exec(database, "COMMIT");
    EXPECT_FALSE(databases_.isInTransaction(database));
}

TEST_F(ExpoSQLiteTest, ACommittedWriteSurvivesInTheFileItsDirectoryIsCreatedFor) {
    {
        SQLiteDatabases databases;
        const std::string path = (fs::path(directory()) / "nested" / "saved.db").string();

        SQLiteDatabases::ensureDirectoryExists(path);
        databases.open(1, path);
        databases.exec(1,
                       "PRAGMA journal_mode = WAL; CREATE TABLE saved (value TEXT); INSERT INTO saved VALUES ('kept')");
    }

    const int reopened = open("nested/saved.db");

    EXPECT_EQ(databases_.run(prepare(reopened, "SELECT value FROM saved"), {}).firstRowValues, (SQLiteRow{"kept"s}));
}

TEST_F(ExpoSQLiteTest, AnInMemoryDatabaseAndABareFileNameNeedNoDirectory) {
    SQLiteDatabases::ensureDirectoryExists(":memory:");
    SQLiteDatabases::ensureDirectoryExists("bare.db");

    const int database = databases_.reserveHandle();

    databases_.open(database, ":memory:");

    EXPECT_FALSE(fs::exists(":memory:"));
    EXPECT_EQ(databases_.run(prepare(database, "SELECT 3"), {}).firstRowValues, (SQLiteRow{3.0}));
}

TEST_F(ExpoSQLiteTest, AWriteBlockedByAnotherConnectionsTransactionFailsWithoutBlockingItsOwnCommit) {
    const int database = open("locked.db");
    const int transaction = open("locked.db");

    databases_.exec(database, "CREATE TABLE entries (value INTEGER)");
    databases_.exec(transaction, "BEGIN; INSERT INTO entries VALUES (1)");
    databases_.exec(database, "BEGIN");

    const int insert = prepare(database, "INSERT INTO entries VALUES (2)");

    EXPECT_EQ(errorOf([&] { static_cast<void>(databases_.run(insert, {})); }), "ExpoSQLite: database is locked");

    databases_.exec(transaction, "COMMIT");
    databases_.exec(database, "COMMIT");
    static_cast<void>(databases_.run(insert, {}));

    EXPECT_EQ(databases_.stepAll(prepare(database, "SELECT value FROM entries ORDER BY value")), (Rows{{1.0}, {2.0}}));
}

TEST_F(ExpoSQLiteTest, AnUnopenableFileFailsToOpen) {
    fs::create_directories(fs::path(directory()) / "a-directory.db");

    EXPECT_THROW(static_cast<void>(open("a-directory.db")), std::runtime_error);
}

TEST_F(ExpoSQLiteTest, TheDirectoryIsWhatTheHostSetAndNothingBefore) {
    EXPECT_EQ(databases_.directory(), "");

    databases_.setDirectory(directory());

    EXPECT_EQ(databases_.directory(), directory());
}

TEST_F(ExpoSQLiteTest, TasksRunInOrderOnOneWorkerThreadAndTheDestructorRunsTheQueuedOnes) {
    std::vector<bool> inTransaction;
    std::vector<std::thread::id> threads;
    std::promise<void> opened;
    std::promise<void> release;
    const std::shared_future<void> released = release.get_future().share();

    {
        SQLiteDatabases databases;
        const int database = databases.reserveHandle();

        databases.post([&, database](SQLiteDatabases& posted) {
            threads.push_back(std::this_thread::get_id());
            posted.open(database, ":memory:");
            opened.set_value();
        });
        opened.get_future().wait();
        databases.post([&, database](SQLiteDatabases& posted) {
            released.wait();
            threads.push_back(std::this_thread::get_id());
            inTransaction.push_back(posted.isInTransaction(database));
        });
        databases.post([&, database](SQLiteDatabases& posted) {
            threads.push_back(std::this_thread::get_id());
            posted.exec(database, "BEGIN");
            inTransaction.push_back(posted.isInTransaction(database));
        });
        release.set_value();
    }

    EXPECT_EQ(inTransaction, (std::vector<bool>{false, true}));
    ASSERT_EQ(threads.size(), 3U);
    EXPECT_EQ(threads[0], threads[1]);
    EXPECT_EQ(threads[1], threads[2]);
    EXPECT_NE(threads[0], std::this_thread::get_id());
}

} // namespace
