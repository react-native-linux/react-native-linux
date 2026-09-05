// The liveness half of issue #263: a requestAnimationFrame loop keeps running while nothing else repaints.
//
// hello_react --fabric packages/core/test-bundles/raf-idle.js
// rnl_window --fabric packages/core/test-bundles/raf-idle.js --frames 240
//
// The bundle commits one static view and then never commits again, so after the first frame there is no mounting
// damage, no scroll and no native animation — nothing that would make the window redraw except the pending rAF
// request itself. `ReactHost::hasPendingTimers` is what reports that request to the frame clock, and a frame
// clock that did not count it would let an occluded or inactive-workspace window stop drawing, which is what
// react-native#57592 looks like from JavaScript: rAF stalls until something else wakes the loop up.
//
// The tick lines are the proof. They are printed every thirtieth callback so a run of a few hundred frames is a
// handful of lines rather than a wall, and the counter is monotonic, so "kept ticking" is an ordered trace
// assertion rather than a screenshot.

const surfaceId = 1;
const fabric = globalThis.nativeFabricUIManager;

if (fabric === undefined) {
  throw new Error('nativeFabricUIManager was not installed');
}

// One unflattened view and nothing else, because the point of the fixture is what happens after the only commit.
const boxHandle = {};
const box = fabric.createNode(
  2,
  'View',
  surfaceId,
  { marginLeft: 40, marginTop: 40, width: 160, height: 90, backgroundColor: 0xff3366cc | 0 },
  boxHandle,
);
const rootChildren = fabric.createChildSet();

fabric.appendChildToSet(rootChildren, box);
fabric.completeRoot(surfaceId, rootChildren);

const TICK_REPORT_INTERVAL = 30;

let tickCount = 0;

const tick = () => {
  tickCount += 1;

  if (tickCount % TICK_REPORT_INTERVAL === 0) {
    console.log('raf-idle: tick ' + tickCount);
  }

  requestAnimationFrame(tick);
};

requestAnimationFrame(tick);

console.log('raf-idle: committed surface ' + surfaceId);
