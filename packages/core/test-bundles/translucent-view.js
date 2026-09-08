// #328's composite-alpha and premultiplication proof. Run under `--transparent-background`, ScenePainter's own
// clear carries no alpha, so every alpha byte in the swapchain image comes from what is actually painted below —
// one half-transparent box, premultiplied by Skia before it ever reaches the compositor.

const manager = globalThis.nativeFabricUIManager;

if (manager === undefined) {
  throw new Error('nativeFabricUIManager was not installed');
}

const surfaceId = 1;
const rootHandle = {};
const boxHandle = {};
const root = manager.createNode(1, 'View', surfaceId, { flex: 1 }, rootHandle);
const translucentBox = manager.createNode(
  2,
  'View',
  surfaceId,
  { width: 200, height: 150, backgroundColor: 0x80cc3366 | 0, marginLeft: 24, marginTop: 24 },
  boxHandle,
);

manager.appendChild(root, translucentBox);

const childSet = manager.createChildSet();

manager.appendChildToSet(childSet, root);
manager.completeRoot(surfaceId, childSet);

console.log('translucent-view: committed surface ' + surfaceId);
