// Issue #419's e2e half: a pointer press delivered during the mount of its own target arrives exactly once and in
// order. The target does not exist until the pointer's first motion reaches the background; that motion mounts it
// under the pointer, and the scenario clicks at once, so the press lands on a node committed in the same breath.
// Upstream Android queues events per tag until an emitter exists; in C++ Fabric the emitter rides the committed
// shadow node, and this is the end-to-end proof that nothing in between drops or repeats the press.

const surfaceId = 1;
const fabric = globalThis.nativeFabricUIManager;

if (fabric === undefined) {
  throw new Error('nativeFabricUIManager was not installed');
}

const pressProps = { onPointerDown: true, onPointerUp: true, onPointerMove: true, onClick: true };

// Fiber-shaped, because `PointerEventsProcessor` resolves a target through `instanceHandle.stateNode.node`.
const handles = [];

const boxAt = (tag, boxName, layout) => {
  const handle = { boxName, stateNode: { node: null } };

  handle.stateNode.node = fabric.createNode(tag, 'View', surfaceId, { position: 'absolute', ...layout, ...pressProps }, handle);
  handles.push(handle);

  return handle.stateNode.node;
};

const background = boxAt(2, 'background', { left: 0, top: 0, width: 800, height: 600, backgroundColor: 0xff1e2430 | 0 });

const commit = (children) => {
  const childSet = fabric.createChildSet();

  children.forEach((child) => fabric.appendChildToSet(childSet, child));
  fabric.completeRoot(surfaceId, childSet);
};

commit([background]);

let targetMounted = false;
let openPresses = 0;

fabric.registerEventHandler((instanceHandle, type) => {
  if (type === 'topPointerMove' && !targetMounted) {
    targetMounted = true;
    commit([background, boxAt(4, 'target', { left: 150, top: 100, width: 100, height: 100, backgroundColor: 0xff3366cc | 0 })]);
    console.log('press-mount: target mounted');

    return;
  }

  if (type !== 'topPointerDown' && type !== 'topPointerUp' && type !== 'topClick') {
    return;
  }

  if (type === 'topPointerDown') {
    openPresses += 1;

    if (openPresses > 1) {
      console.log('press-mount: duplicate down');
    }
  } else if (type === 'topPointerUp') {
    openPresses -= 1;

    if (openPresses < 0) {
      console.log('press-mount: unpaired up');
    }
  }

  console.log('press-mount: ' + type + ' on ' + instanceHandle.boxName);
});

console.log('press-mount: committed surface ' + surfaceId);
