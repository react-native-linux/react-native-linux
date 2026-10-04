// The AsyncStorage proof for issue #23: every method of the RNAsyncStorage TurboModule that
// @react-native-async-storage/async-storage 3.x calls, through the module proxy, against the headless host's
// in-memory store.
//
// hello_react packages/core/test-bundles/async-storage.js

const turboModuleProxy = globalThis.__turboModuleProxy;
const storage =
  typeof turboModuleProxy === 'function'
    ? turboModuleProxy('RNAsyncStorage')
    : globalThis.nativeModuleProxy.RNAsyncStorage;

const prove = async () => {
  const written = await storage.setValues('settings', [
    { key: 'theme', value: 'dark' },
    { key: 'locale', value: 'de' },
  ]);
  console.log(`async-storage: setValues echoed ${written.length} entries`);

  await storage.setValues('other', [{ key: 'theme', value: 'light' }]);
  await storage.removeValues('settings', ['locale']);

  const values = await storage.getValues('settings', ['theme', 'locale']);
  console.log(`async-storage: values ${JSON.stringify(values)}`);
  console.log(`async-storage: keys ${JSON.stringify(await storage.getKeys('other'))}`);

  await storage.clearStorage('other');
  console.log(`async-storage: cleared ${JSON.stringify(await storage.getKeys('other'))}`);

  await storage.legacy_multiSet([['legacy', 'v2']]);
  console.log(`async-storage: legacy ${JSON.stringify(await storage.legacy_multiGet(['legacy', 'absent']))}`);

  await storage.legacy_multiMerge([['legacy', '{}']]).catch((error) => {
    console.log(`async-storage: merge rejected: ${error.message}`);
  });
  await storage.getValues('settings').catch((error) => {
    console.log(`async-storage: missing argument rejected: ${error.message}`);
  });
};

prove().catch((error) => console.log(`async-storage: FAILED ${error.message}`));
