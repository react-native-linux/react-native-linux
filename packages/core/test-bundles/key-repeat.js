// The held-key repeat trace for #65: one focusable view, focused by a click, whose keyDown handler reports the
// payload's `repeat` and `isComposing` and marks the test passed on the first synthesized repeat. Cage supplies
// `wl_keyboard.repeat_info`, so holding the key past its delay is what produces the repeat.
//
// The IME half of the acceptance is `text-input-compose.json`, which drives a composition to a commit.

const surfaceId = 1;
const fabric = globalThis.nativeFabricUIManager;

if (fabric === undefined) {
  throw new Error('nativeFabricUIManager was not installed');
}

const containerHandle = { stateNode: { node: null } };
const container = fabric.createNode(2, 'View', surfaceId, { flex: 1 }, containerHandle);

containerHandle.stateNode.node = container;

const keyTargetHandle = { name: 'key-target', stateNode: { node: null } };
const keyTarget = fabric.createNode(
  3,
  'View',
  surfaceId,
  {
    position: 'absolute',
    left: 60,
    top: 80,
    width: 120,
    height: 60,
    backgroundColor: 0xff3366cc | 0,
    accessible: true,
  },
  keyTargetHandle,
);

keyTargetHandle.stateNode.node = keyTarget;

fabric.registerEventHandler((_instanceHandle, type, payload) => {
  if (type !== 'topKeyDown') {
    return;
  }

  const key = payload === null || payload === undefined ? undefined : payload.key;
  const code = payload === null || payload === undefined ? undefined : payload.code;
  const repeat = payload === null || payload === undefined ? undefined : payload.repeat;
  const isComposing = payload === null || payload === undefined ? undefined : payload.isComposing;

  console.log(
    'key-repeat: ' + type + ' key=' + key + ' code=' + code + ' repeat=' + repeat + ' isComposing=' + isComposing,
  );

  if (repeat === true) {
    console.log('key-repeat: repeat key=' + key);

    if (globalThis.__rnlMarkTestPassed !== undefined) {
      globalThis.__rnlMarkTestPassed();
    }
  }
});

fabric.appendChild(container, keyTarget);

const rootChildren = fabric.createChildSet();

fabric.appendChildToSet(rootChildren, container);
fabric.completeRoot(surfaceId, rootChildren);

console.log('key-repeat: committed surface ' + surfaceId);
