// The #640 proof: an application's own Expo module, packages/test-harness/modules/t3-native-controls, keeps its Linux
// code in linux/ beside android/ and ios/, is autolinked by scripts/autolink.ts from the application's dependency
// entry, built through its linux/CMakeLists.txt, registered by the generated rnl_autolinking.cpp, and called the
// way T3 Code's dpop.ts calls it.

const turboModuleProxy = globalThis.__turboModuleProxy;
const controls =
  typeof turboModuleProxy === 'function'
    ? turboModuleProxy('T3NativeControls')
    : globalThis.nativeModuleProxy.T3NativeControls;
const signingInput = Uint8Array.from('header.payload', (character) => character.charCodeAt(0));

(async () => {
  const key = await controls.generateDpopKey();
  const privateKey = key.slice(0, 32);
  const publicKey = key.slice(32);
  const signature = await controls.signDpop(privateKey, signingInput);
  const matches = await controls.dpopKeyMatches(privateKey, publicKey);

  console.log(`t3-native-controls: key ${key.length} bytes, matches ${matches}, signature ${signature.length} bytes`);
})();
