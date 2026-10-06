// The I18nManager fixture, issue #72: a 400-wide row whose first child sits at its start edge, so its x is the
// surface's layout direction. Each onLayout of the child prints that x and makes the next choice through the
// I18nManager TurboModule — forceRTL(true), then forceRTL(false) — and the row flips and flips back in the same
// running bundle, with no reload. Ending on forceRTL(false) leaves the persisted choice where it started.
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
const nextChoices = [true, false];

fabric.appendChild(row, child);
fabric.appendChild(surface, row);

fabric.registerEventHandler((_instanceHandle, type, payload) => {
  if (type !== 'topLayout') {
    return;
  }

  console.log('i18n-direction: first child at x=' + Math.round(payload.layout.x));

  if (nextChoices.length > 0) {
    i18nManager.forceRTL(nextChoices.shift());
  }
});

const rootChildren = fabric.createChildSet();

fabric.appendChildToSet(rootChildren, surface);
fabric.completeRoot(surfaceId, rootChildren);

console.log('i18n-direction: committed surface ' + surfaceId);
