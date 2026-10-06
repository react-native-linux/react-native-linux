// Issue #435's e2e half: during a resize drag, each node that changed gets at most one onLayout per configure, a node
// that did not change gets none, and nothing follows the last configure. The fixture is three absolutely placed
// boxes: `half` takes half the window, `fixed` never changes, and `feedback` is the feedback case, whose height
// this bundle derives from `half`'s reported width and commits from inside the onLayout handler.

const surfaceId = 1;
const fabric = globalThis.nativeFabricUIManager;

if (fabric === undefined) {
  throw new Error('nativeFabricUIManager was not installed');
}

// Named on the handle, which is what a topLayout comes back with; retained, because C++ holds handles weakly.
const handles = [];

const box = (tag, name, style) => {
  const handle = { name, stateNode: { node: null } };

  handles.push(handle);
  handle.stateNode.node = fabric.createNode(tag, 'View', surfaceId, { position: 'absolute', onLayout: true, ...style }, handle);

  return handle;
};

const half = box(2, 'half', { left: 0, top: 0, width: '50%', height: 40, backgroundColor: 0xff61afef | 0 });
const fixed = box(4, 'fixed', { left: 0, top: 50, width: 120, height: 40, backgroundColor: 0xffe5c07b | 0 });
const feedback = box(6, 'feedback', { left: 0, top: 100, width: 60, height: 10, backgroundColor: 0xff98c379 | 0 });

const commit = () => {
  const children = fabric.createChildSet();

  [half, fixed, feedback].forEach((handle) => fabric.appendChildToSet(children, handle.stateNode.node));
  fabric.completeRoot(surfaceId, children);
};

commit();

// One onLayout per distinct frame per node: BaseViewEventEmitter drops a frame equal to the last one it sent, so a
// second event carrying the same frame for the same node is the redundant wake-up this issue is about.
const seenFrames = new Map();
let fixedLayouts = 0;
let expectedFeedback = null;

fabric.registerEventHandler((instanceHandle, type, payload) => {
  if (type !== 'topLayout') {
    return;
  }

  const { x, y, width, height } = payload.layout;
  const key = instanceHandle.name + ' ' + [x, y, width, height].join(',');

  if (seenFrames.has(key)) {
    console.log('onlayout-drag: redundant onLayout for ' + key);
  }

  seenFrames.set(key, true);

  if (instanceHandle.name === 'fixed') {
    fixedLayouts += 1;

    if (fixedLayouts > 1) {
      console.log('onlayout-drag: unchanged node relaid out');
    }
  }

  if (instanceHandle.name === 'half') {
    expectedFeedback = { halfWidth: Math.round(width), height: Math.round(width / 10) };
    feedback.stateNode.node = fabric.cloneNodeWithNewProps(feedback.stateNode.node, {
      height: expectedFeedback.height,
    });
    commit();
  }

  // The feedback commit counts only once its own layout comes back with the derived height.
  if (instanceHandle.name === 'feedback' && expectedFeedback !== null && height === expectedFeedback.height) {
    console.log('onlayout-drag: half ' + expectedFeedback.halfWidth + ' feedback ' + height);
    expectedFeedback = null;
  }
});

console.log('onlayout-drag: committed surface ' + surfaceId);
