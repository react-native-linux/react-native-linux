// The I18nManager fixture, issue #72: a 400-wide row whose first child sits at its start edge, so its x is the
// surface's layout direction. Each onLayout of the child prints that x and makes the next choice through the
// I18nManager TurboModule — forceRTL(true), then forceRTL(false) — and the row flips and flips back in the same
// running bundle, with no reload. The run's own app id keeps its persisted choice away from every other scenario.
//
// rnl_window --app-id org.reactnative.linux.e2e.i18n --fabric packages/core/test-bundles/i18n-direction.js

const surfaceId = 1;
const fabric = globalThis.nativeFabricUIManager;
const turboModuleProxy = globalThis.__turboModuleProxy;
const i18nManager =
  typeof turboModuleProxy === 'function' ? turboModuleProxy('I18nManager') : globalThis.nativeModuleProxy.I18nManager;

if (fabric === undefined) {
  throw new Error('nativeFabricUIManager was not installed');
}

// C++ holds instance handles weakly, so the fixture is what keeps them alive.
const handles = [];

const createView = (tag, props) => {
  const handle = { stateNode: { node: null } };

  handle.stateNode.node = fabric.createNode(tag, 'View', surfaceId, props, handle);
  handles.push(handle);

  return handle.stateNode.node;
};

const child = createView(2, { width: 100, height: 100, backgroundColor: 0xff61afef | 0, onLayout: true });
const row = createView(3, { flexDirection: 'row', width: 400, height: 100, backgroundColor: 0xff11141a | 0 });
const surface = createView(4, { flex: 1, backgroundColor: 0xff282c34 | 0 });
// Whatever the runner's locale or a stale persisted choice says, the run starts from left-to-right: disallowing RTL
// and unforcing it makes the direction LTR from the next frame on. The steps below then wait for the child to be
// seen at the start edge before flipping, so a first layout that came up RTL is waited out rather than misread.
i18nManager.allowRTL(false);
i18nManager.forceRTL(false);

const steps = [
  { atX: 0, then: () => i18nManager.forceRTL(true) },
  { atX: 300, then: () => i18nManager.forceRTL(false) },
  { atX: 0, then: () => {} },
];

fabric.appendChild(row, child);
fabric.appendChild(surface, row);

fabric.registerEventHandler((_instanceHandle, type, payload) => {
  if (type !== 'topLayout' || steps.length === 0) {
    return;
  }

  const x = Math.round(payload.layout.x);

  if (x !== steps[0].atX) {
    return;
  }

  console.log('i18n-direction: first child at x=' + x);
  steps.shift().then();
});

const rootChildren = fabric.createChildSet();

fabric.appendChildToSet(rootChildren, surface);
fabric.completeRoot(surfaceId, rootChildren);

console.log('i18n-direction: committed surface ' + surfaceId);
