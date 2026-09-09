// The event-trace fixture for issue #367's e2e half: one full-content view that logs every pointer event Fabric
// delivers to it, so a scenario can prove that a press in the drawn titlebar produced a window request (`move`,
// `resize`, `window-menu`) while a press in the content reached the node instead.
//
// The view spans the content area — everything below the drawn titlebar's 32-pixel band — so the scenario's
// content press lands on it wherever it lands, and the gutter presses land in the chrome, which routes them
// away from the scene before they ever hit-test.

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
  { flex: 1, backgroundColor: 0xff1e2430 | 0, onPointerDown: true, onPointerUp: true },
  instanceHandle,
);

instanceHandle.stateNode.node = content;

fabric.registerEventHandler((handle, type, payload) => {
  const name = handle === instanceHandle ? 'content' : 'unknown';

  console.log('decorations-target: ' + type + ' on ' + name + ' at ' + payload.clientX + ',' + payload.clientY);
});

const rootChildren = fabric.createChildSet();

fabric.appendChildToSet(rootChildren, content);
fabric.completeRoot(surfaceId, rootChildren);

console.log('decorations-target: committed surface ' + surfaceId);
