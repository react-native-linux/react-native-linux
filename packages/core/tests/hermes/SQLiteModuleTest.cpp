#include "ExpoSQLite.h"
#include "OnJavaScriptThread.h"
#include "ReactHost.h"

#include <cxxreact/JSBigString.h>
#include <filesystem>
#include <gtest/gtest.h>
#include <memory>
#include <string>
#include <unistd.h>

namespace react_native_linux {
namespace {

/**
 * The native calls upstream expo-sqlite 58's JavaScript makes for what T3 Code calls, in its order and with its
 * argument shapes: `createTurboModuleToExpoProxy` spreads `getConstants()` onto the module, `openDatabaseAsync`
 * ensures the directory, constructs a `NativeDatabase` and awaits `initAsync`, every query prepares a fresh
 * `NativeStatement`, runs it with `normalizeParams`' `(values, blobValues, shouldPassAsArray)` and finalizes it, and
 * `withExclusiveTransactionAsync` opens a second connection to the same path. The helpers with a rest parameter
 * are `async function`s because this Hermes resumes the caller of an async arrow with one too early (#659).
 */
constexpr char kUpstreamCalls[] = R"JAVASCRIPT(
var module = globalThis.nativeModuleProxy.ExpoSQLite;
var ExpoSQLite = { ...module.getConstants(), ensureDatabasePathExistsAsync: module.ensureDatabasePathExistsAsync };
var trace = [];

var openDatabaseAsync = async (name) => {
  const databasePath = `${ExpoSQLite.defaultDatabaseDirectory}/${name}`;
  await ExpoSQLite.ensureDatabasePathExistsAsync(databasePath);
  const database = new ExpoSQLite.NativeDatabase(databasePath, {});
  await database.initAsync();
  return database;
};
var prepareAsync = async (database, source) => {
  const statement = new ExpoSQLite.NativeStatement();
  await database.prepareAsync(statement, source);
  return statement;
};
var normalize = (params) => [Object.fromEntries(params.map((value, index) => [index, value])), {}, true];
var composeRow = (names, values) => Object.fromEntries(names.map((name, index) => [name, values[index]]));
async function runAsync(database, source, ...params) {
  const statement = await prepareAsync(database, source);
  try {
    const { lastInsertRowId, changes } = await statement.runAsync(database, ...normalize(params));
    return { lastInsertRowId, changes };
  } finally {
    await statement.finalizeAsync(database);
  }
}
async function getFirstAsync(database, source, ...params) {
  const statement = await prepareAsync(database, source);
  try {
    const { firstRowValues } = await statement.runAsync(database, ...normalize(params));
    const names = await statement.getColumnNamesAsync();
    const values = firstRowValues.length > 0 ? firstRowValues : await statement.stepAsync(database);
    return values == null ? null : composeRow(names, values);
  } finally {
    await statement.finalizeAsync(database);
  }
}
async function getAllAsync(database, source, ...params) {
  const statement = await prepareAsync(database, source);
  try {
    const { firstRowValues } = await statement.runAsync(database, ...normalize(params));
    if (firstRowValues.length === 0) {
      return [];
    }
    const names = await statement.getColumnNamesAsync();
    return [firstRowValues, ...(await statement.getAllAsync(database))].map((values) => composeRow(names, values));
  } finally {
    await statement.finalizeAsync(database);
  }
}
var withExclusiveTransactionAsync = async (database, task) => {
  const transaction = await openDatabaseAsync('module.db');
  try {
    await transaction.execAsync('BEGIN');
    await task(transaction);
    await transaction.execAsync('COMMIT');
  } catch (error) {
    await transaction.execAsync('ROLLBACK');
    throw error;
  } finally {
    await transaction.closeAsync();
  }
};
var recordRejection = (promise) => promise.then(() => trace.push('resolved'), (error) => trace.push(error.message));
var finish = (probe) =>
  probe()
    .catch((error) => trace.push(`FAILED ${error.message}`))
    .finally(() => {
      globalThis.sqliteTrace = trace.join(' | ');
    });
)JAVASCRIPT";

constexpr char kWriteProbe[] = R"JAVASCRIPT(
finish(async () => {
  const database = await openDatabaseAsync('module.db');
  await database.execAsync(
    'PRAGMA journal_mode = WAL; CREATE TABLE entries (id INTEGER PRIMARY KEY, label TEXT NOT NULL, note TEXT)');
  await withExclusiveTransactionAsync(database, async (transaction) => {
    trace.push(`in transaction ${await transaction.isInTransactionAsync()}`);
    const { lastInsertRowId, changes } = await runAsync(transaction, 'INSERT INTO entries (label) VALUES (?)', 'first');
    trace.push(`inserted ${lastInsertRowId} ${changes}`);
    await runAsync(transaction, 'INSERT INTO entries (label, note) VALUES (?, ?)', 'second', null);
  });
  await recordRejection(withExclusiveTransactionAsync(database, async (transaction) => {
    await runAsync(transaction, 'INSERT INTO entries (label) VALUES (?)', 'rolled back');
    await runAsync(transaction, 'INSERT INTO entries (label) VALUES (?)', null);
  }));
  trace.push(JSON.stringify(await getAllAsync(database, 'SELECT * FROM entries ORDER BY id')));
  trace.push(JSON.stringify(await getFirstAsync(database, 'SELECT label FROM entries WHERE id = ?', 2)));
  trace.push(JSON.stringify(await getFirstAsync(database, 'SELECT label FROM entries WHERE id = ?', 9)));
  trace.push(JSON.stringify(await getAllAsync(database, 'SELECT label FROM entries WHERE id = ?', 9)));
  trace.push(`in transaction ${await database.isInTransactionAsync()}`);

  const statement = await prepareAsync(database, 'SELECT ?');
  await recordRejection(Promise.resolve().then(() => statement.runAsync(database, { 0: new Uint8Array(1) }, {}, true)));
  await recordRejection(Promise.resolve().then(() => statement.runAsync(database, {}, { 0: new Uint8Array(1) }, true)));
  await recordRejection(statement.resetAsync(database));
  await statement.finalizeAsync(database);
  await recordRejection(statement.stepAsync(database));
  await recordRejection(Promise.resolve().then(() => database.prepareAsync({}, 'SELECT 1')));
  await recordRejection(Promise.resolve().then(() => new ExpoSQLite.NativeDatabase()));
  await recordRejection(Promise.resolve().then(() => new ExpoSQLite.NativeDatabase(':memory:', {}, new Uint8Array(1))));
  await recordRejection(Promise.resolve().then(
    () => new ExpoSQLite.NativeDatabase(':memory:', { enableChangeListener: true })));
  await recordRejection(Promise.resolve().then(() => new ExpoSQLite.NativeDatabase(
    ':memory:', { useNewConnection: true, finalizeUnusedStatementsBeforeClosing: true, enableChangeListener: false,
                  libSQLOptions: null }, undefined)));
  await database.closeAsync();
  await recordRejection(database.execAsync('SELECT 1'));
});
)JAVASCRIPT";

constexpr char kReadProbe[] = R"JAVASCRIPT(
finish(async () => {
  const database = await openDatabaseAsync('module.db');
  trace.push(JSON.stringify(await getAllAsync(database, 'SELECT label FROM entries ORDER BY id')));
  await database.closeAsync();
});
)JAVASCRIPT";

constexpr char kHeadlessProbe[] = R"JAVASCRIPT(
globalThis.sqliteTrace = String(ExpoSQLite.defaultDatabaseDirectory);
)JAVASCRIPT";

void loadProbe(ReactHost& reactHost, const char* probe, const char* name) {
    reactHost.loadScript(std::make_unique<facebook::react::JSBigStdString>(kUpstreamCalls), "SQLiteUpstreamCalls.js");
    reactHost.loadScript(std::make_unique<facebook::react::JSBigStdString>(probe), name);
}

/**
 * #638: `ExpoSQLite` driven exactly as upstream expo-sqlite's JavaScript drives it. Two transactions on their own
 * connection, one committed and one rolled back by a failed insert, rows composed from column names and values, the
 * first row of a match and of no match, and rejections for a value of an unsupported type and a BLOB, a reset that
 * resolves, a finalized statement, an object that is not a statement, a constructor without its path, serialized
 * bytes to deserialize, an enabled open option it does not support, and a closed database. A second host on the same
 * directory, which is what a reload is, reads back what the first committed.
 */
TEST(SQLiteModuleTest, UpstreamsCallsWriteInTransactionsAndTheNextInstanceReadsThemBack) {
    const std::filesystem::path directory =
        std::filesystem::temp_directory_path() / ("rnl-sqlite-module-test-" + std::to_string(::getpid()));

    std::filesystem::remove_all(directory);

    {
        ReactHost reactHost;

        reactHost.sqliteDatabases().setDirectory(directory.string());
        loadProbe(reactHost, kWriteProbe, "SQLiteWrite.js");

        EXPECT_EQ(awaitGlobalString(reactHost, "sqliteTrace"),
                  "in transaction true | inserted 1 1 | ExpoSQLite: NOT NULL constraint failed: entries.label | "
                  R"([{"id":1,"label":"first","note":null},{"id":2,"label":"second","note":null}] | )"
                  R"({"label":"second"} | null | [] | in transaction false | )"
                  "Exception in HostFunction: ExpoSQLite: parameter 0 is not a string, a number or null | "
                  "Exception in HostFunction: ExpoSQLite: BLOB parameters are not supported on Linux | resolved | "
                  "ExpoSQLite: the statement is finalized | "
                  "Exception in HostFunction: ExpoSQLite: not a NativeDatabase or a NativeStatement | "
                  "Exception in HostFunction: ExpoSQLite: argument 0 is missing | "
                  "Exception in HostFunction: ExpoSQLite: deserializing a database is not supported on Linux | "
                  "Exception in HostFunction: ExpoSQLite: the open option enableChangeListener is not supported on "
                  "Linux | resolved | "
                  "ExpoSQLite: the database is closed");
        EXPECT_FALSE(reactHost.hasReportedFatalError());
    }

    ReactHost reloaded;

    reloaded.sqliteDatabases().setDirectory(directory.string());
    loadProbe(reloaded, kReadProbe, "SQLiteRead.js");

    EXPECT_EQ(awaitGlobalString(reloaded, "sqliteTrace"), R"([{"label":"first"},{"label":"second"}])");
    EXPECT_FALSE(reloaded.hasReportedFatalError());
    std::filesystem::remove_all(directory);
}

/** A host with no application directory answers none, so upstream's `createDatabasePath` names the problem. */
TEST(SQLiteModuleTest, AHeadlessHostHasNoDefaultDirectory) {
    ReactHost reactHost;

    loadProbe(reactHost, kHeadlessProbe, "SQLiteHeadless.js");

    EXPECT_EQ(awaitGlobalString(reactHost, "sqliteTrace"), "null");
}

} // namespace
} // namespace react_native_linux
