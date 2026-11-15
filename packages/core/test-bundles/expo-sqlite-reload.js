// #638: the persistence T3 Code's MobileDatabase relies on, in the real window. The bundle drives the ExpoSQLite
// TurboModule with the calls upstream expo-sqlite's JavaScript makes: a NativeDatabase under the module's default
// directory, a fresh NativeStatement per query, and a second connection for the exclusive transaction. The first
// instance writes a row inside that transaction and reloads through DevSettings; the second reads the row back from
// the file the first committed under the XDG_DATA_HOME the e2e driver keeps for the whole run.
//
// rnl_window packages/core/test-bundles/expo-sqlite-reload.js --frames 600

const turboModuleProxy = globalThis.__turboModuleProxy;
const nativeModule = (name) =>
  typeof turboModuleProxy === 'function' ? turboModuleProxy(name) : globalThis.nativeModuleProxy[name];
const sqlite = nativeModule('ExpoSQLite');
const { NativeDatabase, NativeStatement, defaultDatabaseDirectory } = sqlite.getConstants();
const databasePath = `${defaultDatabaseDirectory}/e2e.db`;

if (defaultDatabaseDirectory == null) {
  throw new Error('Both provided directory and defaultDatabaseDirectory are null.');
}

console.log(`expo-sqlite: directory ${defaultDatabaseDirectory.slice(defaultDatabaseDirectory.indexOf('/data/'))}`);

const openDatabase = async () => {
  await sqlite.ensureDatabasePathExistsAsync(databasePath);
  const database = new NativeDatabase(databasePath, {});
  await database.initAsync();
  return database;
};

async function query(database, source, ...params) {
  const statement = new NativeStatement();
  await database.prepareAsync(statement, source);
  try {
    const values = Object.fromEntries(params.map((value, index) => [index, value]));
    const { firstRowValues } = await statement.runAsync(database, values, {}, true);
    return firstRowValues.length === 0 ? [] : [firstRowValues, ...(await statement.getAllAsync(database))];
  } finally {
    await statement.finalizeAsync(database);
  }
}

const prove = async () => {
  const database = await openDatabase();
  await database.execAsync(
    'PRAGMA journal_mode = WAL; CREATE TABLE IF NOT EXISTS instances (instance INTEGER PRIMARY KEY NOT NULL)');
  const previous = (await query(database, 'SELECT instance FROM instances ORDER BY instance')).map(([value]) => value);
  const instance = previous.length + 1;
  const transaction = await openDatabase();
  try {
    await transaction.execAsync('BEGIN');
    await query(transaction, 'INSERT INTO instances (instance) VALUES (?)', instance);
    await transaction.execAsync('COMMIT');
  } finally {
    await transaction.closeAsync();
  }
  const [[count]] = await query(database, 'SELECT COUNT(*) FROM instances');
  await database.closeAsync();
  console.log(`expo-sqlite: instance ${instance} read ${JSON.stringify(previous)} count ${count}`);

  if (instance === 1) {
    nativeModule('DevSettings').reload();
  }
};

prove().catch((error) => console.log(`expo-sqlite: FAILED ${error.message}`));
