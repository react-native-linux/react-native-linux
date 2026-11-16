// The idle-then-wake proof of issue #703: a visible window with nothing to do stops presenting, and a tap wakes it.
//
// rnl_window --fabric packages/core/test-bundles/idle-wake.js --frames 720
//
// The bundle commits once and then has no damage, no animation, no timer and no requestAnimationFrame pending, so the
// window must stop presenting once its swapchain settles, and stay stopped until the click. The click starts a
// requestAnimationFrame loop that moves a box and commits once per frame for ANIMATED_FRAMES frames, then stops, so
// the frame log shows an idle gap, a run of animated frames answering the click, and idle again. The scenario gates
// the gap on `wp_presentation` timestamps.

const surfaceId = 1;
const fabric = globalThis.nativeFabricUIManager;

if (fabric === undefined) {
  throw new Error('nativeFabricUIManager was not installed');
}

const ANIMATED_FRAMES = 120;
const STEP = 4;

// PointerEventsProcessor resolves an event target through instanceHandle.stateNode.node, the shape React's fiber has.
const targetHandle = { stateNode: { node: null } };
const movingHandle = { stateNode: { node: null } };

const box = (left, top, width, height, backgroundColor) => ({
  position: 'absolute',
  left,
  top,
  width,
  height,
  backgroundColor,
});

targetHandle.stateNode.node = fabric.createNode(
  2,
  'View',
  surfaceId,
  { ...box(100, 80, 200, 120, 0xff3366cc | 0), onPointerDown: true, onPointerUp: true, onClick: true },
  targetHandle,
);

const movingStyle = (left) => box(left, 240, 60, 60, 0xffe5c07b | 0);

movingHandle.stateNode.node = fabric.createNode(4, 'View', surfaceId, movingStyle(100), movingHandle);

const commit = () => {
  const children = fabric.createChildSet();

  fabric.appendChildToSet(children, targetHandle.stateNode.node);
  fabric.appendChildToSet(children, movingHandle.stateNode.node);
  fabric.completeRoot(surfaceId, children);
};

let animatedFrames = 0;

const step = () => {
  animatedFrames += 1;
  movingHandle.stateNode.node = fabric.cloneNodeWithNewProps(
    movingHandle.stateNode.node,
    movingStyle(100 + animatedFrames * STEP),
  );
  commit();

  if (animatedFrames < ANIMATED_FRAMES) {
    requestAnimationFrame(step);
  } else {
    console.log('idle-wake: animated ' + animatedFrames + ' frames');
  }
};

fabric.registerEventHandler((instanceHandle, type) => {
  if (instanceHandle === targetHandle && type === 'topClick' && animatedFrames === 0) {
    console.log('idle-wake: clicked');
    requestAnimationFrame(step);
  }
});

commit();

console.log('idle-wake: committed surface ' + surfaceId);
