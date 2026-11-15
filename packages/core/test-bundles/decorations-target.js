// The event-trace fixture for issue #367's e2e half: one full-content view that logs every pointer event Fabric
// delivers to it, so a scenario can prove that a press in the drawn titlebar produced a window request (`move`,
// `resize`, `window-menu`) while a press in the content reached the node instead.
//
// The view spans the content area — everything below the drawn titlebar's 32-pixel band — so the scenario's
// content press lands on it wherever it lands, and the gutter presses land in the chrome, which routes them
// away from the scene before they ever hit-test.
//
// It also prints the view's layout beside the window size `Dimensions` last announced (#374), on every change to
// either, so a scenario can assert that the Yoga root and `Dimensions` agree on the size a configure implied. The
// two arrive on separate paths in no fixed order, so the line that settles a configure is the one where both
// already carry its size.

const surfaceId = 1;
const fabric = globalThis.nativeFabricUIManager;

if (fabric === undefined) {
  throw new Error('nativeFabricUIManager was not installed');
}

const instanceHandle = { stateNode: { node: null } };
const content = fabric.createNode(
  2,
  'View',
  surfaceId,
  { flex: 1, backgroundColor: 0xff1e2430 | 0, onPointerDown: true, onPointerUp: true, onLayout: true },
  instanceHandle,
);

instanceHandle.stateNode.node = content;

const sizes = { layout: 'none', window: 'none' };

const reportSizes = () => {
  console.log('decorations-target: layout ' + sizes.layout + ' window ' + sizes.window);
};

globalThis.__rctDeviceEventEmitter = {
  emit: (eventName, payload) => {
    if (eventName === 'didUpdateDimensions') {
      sizes.window = payload.window.width + 'x' + payload.window.height;
      reportSizes();
    }
  },
};

fabric.registerEventHandler((handle, type, payload) => {
  if (type === 'topLayout') {
    sizes.layout = Math.round(payload.layout.width) + 'x' + Math.round(payload.layout.height);
    reportSizes();

    return;
  }

  const name = handle === instanceHandle ? 'content' : 'unknown';

  console.log('decorations-target: ' + type + ' on ' + name + ' at ' + payload.clientX + ',' + payload.clientY);
});

const rootChildren = fabric.createChildSet();

fabric.appendChildToSet(rootChildren, content);
fabric.completeRoot(surfaceId, rootChildren);

console.log('decorations-target: committed surface ' + surfaceId);
