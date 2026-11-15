// #684: T3 Code's modules/t3-highlights, with packages/test-harness/modules/t3-highlights/linux added, autolinked from
// the application's dependency entry and called the way T3 Code's highlights.ts calls it. 139 is the tsx lexer in
// T3 Code's highlights.generated.ts. CI compares the pairs with those @pierre/highlights' WebAssembly build gives for
// the same source, which is what T3 Code's own highlights.test.ts holds the iOS and Android module to.

const turboModuleProxy = globalThis.__turboModuleProxy;
const highlights =
  typeof turboModuleProxy === 'function'
    ? turboModuleProxy('T3Highlights')
    : globalThis.nativeModuleProxy.T3Highlights;
const records = highlights.tokenize('const greeting = `hi ${name}`; // é\r\nexport default greeting;\n', 139);

console.log(`t3-highlights: ${Array.from(new Uint32Array(records)).join(',')}`);
