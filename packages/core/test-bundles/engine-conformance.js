// The engine-parity fixture for issue #82: one line per language feature, run once from source and once from
// `hermesc -O` bytecode, and the two outputs must be identical. react-native-windows#11952 — Array.prototype.flat()
// breaking in release builds only — is the class of bug this exists to catch.
//
// hello_react packages/core/test-bundles/engine-conformance.js
// hermesc -O -emit-binary -out /tmp/engine-conformance.hbc packages/core/test-bundles/engine-conformance.js
// hello_react /tmp/engine-conformance.hbc

const checks = {
  'Array.prototype.flat': () => [1, [2, [3, [4]]]].flat(Infinity).join() === '1,2,3,4',
  'Array.prototype.flatMap': () => [1, 2].flatMap((value) => [value, value * 10]).join() === '1,10,2,20',
  'Array.prototype.at': () => [1, 2, 3].at(-1) === 3,
  'Array.prototype.findLast': () => [1, 2, 3, 4].findLast((value) => value % 2 === 1) === 3,
  'Object.fromEntries': () => Object.fromEntries([['a', 1]]).a === 1,
  'optional chaining and nullish coalescing': () => ({ a: null }).a?.b ?? 'fallback' === 'fallback',
  'String.prototype.replaceAll': () => 'a-b-c'.replaceAll('-', '+') === 'a+b+c',
  'String.prototype.padStart': () => '7'.padStart(3, '0') === '007',
  'RegExp named groups': () => /(?<year>\d{4})/.exec('in 2026').groups.year === '2026',
  'RegExp lookbehind': () => /(?<=\$)\d+/.exec('cost $42')[0] === '42',
  'RegExp sticky and unicode': () => /\u{1F600}/u.test('\u{1F600}') && /a/y.sticky,
  'destructuring with defaults and rest': () => {
    const { a = 1, ...rest } = { b: 2, c: 3 };
    return a === 1 && Object.keys(rest).join() === 'b,c';
  },
  'classes, getters and static members': () => {
    class Shape {
      static kind = 'shape';
      constructor(side) {
        this.side = side;
      }
      get area() {
        return this.side * this.side;
      }
    }
    return new Shape(3).area === 9 && Shape.kind === 'shape';
  },
  'generators': () => {
    function* count() {
      yield 1;
      yield 2;
    }
    return [...count()].join() === '1,2';
  },
  'Map, Set and WeakMap': () => {
    const key = {};
    const weak = new WeakMap([[key, 'v']]);
    return new Map([[1, 'a']]).get(1) === 'a' && new Set([1, 1, 2]).size === 2 && weak.get(key) === 'v';
  },
  'Symbol.iterator protocol': () => {
    const iterable = { *[Symbol.iterator]() { yield 'x'; } };
    return Array.from(iterable).join() === 'x';
  },
  'Proxy and Reflect': () => {
    const proxy = new Proxy({}, { get: (_target, name) => `got ${String(name)}` });
    return proxy.anything === 'got anything' && Reflect.has({ a: 1 }, 'a');
  },
  'BigInt': () => (2n ** 64n).toString() === '18446744073709551616',
  'typed arrays and DataView': () => {
    const view = new DataView(new ArrayBuffer(4));
    view.setUint16(0, 0xbeef);
    return new Uint8Array(view.buffer)[0] === 0xbe;
  },
  'JSON round trip': () => JSON.stringify(JSON.parse('{"a":[1,{"b":null}]}')) === '{"a":[1,{"b":null}]}',
  'Date arithmetic': () => new Date(Date.UTC(2026, 0, 31) + 86400000).getUTCMonth() === 1,
  'template literals and tagged templates': () => {
    const tag = (strings, ...values) => strings.raw.join('|') + values.join();
    return tag`a${1}b` === 'a|b1';
  },
  'labelled break and switch': () => {
    let hits = 0;
    outer: for (const row of [[1, 2], [3, 4]]) {
      for (const value of row) {
        switch (value) {
          case 3:
            break outer;
          default:
            hits += 1;
        }
      }
    }
    return hits === 2;
  },
  'getter on Function.prototype.name': () => function named() {}.name === 'named',
  'Error cause': () => new Error('outer', { cause: 'inner' }).cause === 'inner',
};

const results = Object.entries(checks).map(([name, check]) => {
  let passed = false;

  try {
    passed = check() === true;
  } catch (error) {
    passed = false;
  }

  console.log(`engine: ${name} ${passed ? 'ok' : 'FAILED'}`);

  return passed;
});

let asyncPassed = false;

(async () => {
  const value = await Promise.resolve(41);
  asyncPassed = value + 1 === 42;
})().then(() => {
  console.log(`engine: async/await ${asyncPassed ? 'ok' : 'FAILED'}`);
  const passedCount = results.filter(Boolean).length + (asyncPassed ? 1 : 0);
  console.log(`engine: ${passedCount}/${results.length + 1} passed`);
});
