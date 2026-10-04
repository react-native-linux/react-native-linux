#include "AsyncStorage.h"

#include <filesystem>
#include <gtest/gtest.h>
#include <optional>
#include <sqlite3.h>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using react_native_linux::KeyValueStore;

namespace fs = std::filesystem;

using Entries = std::vector<KeyValueStore::Entry>;

class AsyncStorageTest : public ::testing::Test {
protected:
    void SetUp() override {
        root_ = fs::temp_directory_path() /
                ("rnl-async-storage-" + std::string(::testing::UnitTest::GetInstance()->current_test_info()->name()));
        fs::remove_all(root_);
    }

    void TearDown() override { fs::remove_all(root_); }

    std::string databasePath() const { return (root_ / "app" / "async-storage.sqlite").string(); }

    fs::path root_;
};

TEST_F(AsyncStorageTest, AStoredValueReadsBackAndAnAbsentKeyReadsAsNull) {
    KeyValueStore store;

    store.set("settings", {{"theme", "dark"}});

    EXPECT_EQ(store.get("settings", {"theme", "missing"}), (Entries{{"theme", "dark"}, {"missing", std::nullopt}}));
}

TEST_F(AsyncStorageTest, EachNamedDatabaseIsItsOwnKeySpace) {
    KeyValueStore store;

    store.set("first", {{"shared", "one"}, {"only-first", "x"}});
    store.set("second", {{"shared", "two"}});
    store.clear("second");

    EXPECT_EQ(store.get("first", {"shared"}), (Entries{{"shared", "one"}}));
    EXPECT_EQ(store.keys("first"), (std::vector<std::string>{"only-first", "shared"}));
    EXPECT_TRUE(store.keys("second").empty());
}

TEST_F(AsyncStorageTest, ANullValueAndRemoveBothDeleteTheKey) {
    KeyValueStore store;

    store.set("db", {{"a", "1"}, {"b", "2"}, {"c", "3"}});
    store.set("db", {{"a", std::nullopt}, {"b", "20"}});
    store.remove("db", {"c"});

    EXPECT_EQ(store.keys("db"), (std::vector<std::string>{"b"}));
    EXPECT_EQ(store.get("db", {"b"}), (Entries{{"b", "20"}}));
}

TEST_F(AsyncStorageTest, ValuesSurviveTheProcessInTheFileTheDirectoryIsCreatedFor) {
    {
        KeyValueStore store;

        store.setDatabasePath(databasePath());
        store.set("db", {{"saved", "yes"}});
    }

    KeyValueStore reopened;

    reopened.setDatabasePath(databasePath());

    EXPECT_EQ(reopened.get("db", {"saved"}), (Entries{{"saved", "yes"}}));
}

TEST_F(AsyncStorageTest, MovingTheDatabaseClosesTheInMemoryOne) {
    KeyValueStore store;

    store.set("db", {{"ephemeral", "yes"}});
    store.setDatabasePath(databasePath());

    EXPECT_TRUE(store.keys("db").empty());
}

TEST_F(AsyncStorageTest, ABatchThatFailsPartWayLandsNothingAndNamesTheCause) {
    KeyValueStore store;

    store.setDatabasePath(databasePath());
    store.set("db", {{"existing", "kept"}});

    sqlite3* saboteur = nullptr;
    ASSERT_EQ(sqlite3_open(databasePath().c_str(), &saboteur), SQLITE_OK);
    ASSERT_EQ(sqlite3_exec(saboteur,
                           "CREATE TRIGGER refuse BEFORE INSERT ON entries WHEN NEW.entry_key = 'poison' "
                           "BEGIN SELECT RAISE(ABORT, 'poisoned batch'); END",
                           nullptr, nullptr, nullptr),
              SQLITE_OK);
    sqlite3_close(saboteur);

    EXPECT_THROW(
        {
            try {
                store.set("db", {{"first", "1"}, {"poison", "2"}});
            } catch (const std::runtime_error& error) {
                EXPECT_STREQ(error.what(), "AsyncStorage: poisoned batch");
                throw;
            }
        },
        std::runtime_error);
    EXPECT_EQ(store.keys("db"), (std::vector<std::string>{"existing"}));
}

} // namespace
