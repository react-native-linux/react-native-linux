// The fixture for #341: a 200x150 <ScrollView> whose 400x600 content scrolls on both axes, so an unlocked
// two-finger pan with a five-degree drift would move the horizontal offset too. The gesture arrives as continuous
// `axis` deltas on both axes in one frame, and the platform's dominant-axis lock must keep horizontal at zero
// while vertical moves.
//
// The trace is the assertion: every scroll reports its offset, a non-zero horizontal offset prints a drift line
// the scenario rejects, and the first real vertical movement prints the line the scenario requires.

const surfaceId = 1;
const fabric = globalThis.nativeFabricUIManager;

if (fabric === undefined) {
  throw new Error('nativeFabricUIManager was not installed');
}

const pageHandle = { stateNode: { node: null } };
const page = fabric.createNode(
  3,
  'ScrollView',
  surfaceId,
  {
    position: 'absolute',
    left: 60,
    top: 40,
    width: 200,
    height: 150,
    backgroundColor: 0xff1e2430 | 0,
    onScroll: true,
  },
  pageHandle,
);

pageHandle.stateNode.node = page;

const contentHandle = { stateNode: { node: null } };
const content = fabric.createNode(
  4,
  'View',
  surfaceId,
  {
    position: 'absolute',
    left: 0,
    top: 0,
    width: 400,
    height: 600,
    backgroundColor: 0xff3366cc | 0,
  },
  contentHandle,
);

contentHandle.stateNode.node = content;

let reportedVerticalMovement = false;

fabric.registerEventHandler((_instanceHandle, type, payload) => {
  if (type !== 'topScroll' || payload === null || payload === undefined) {
    return;
  }

  const offset = payload.contentOffset;

  if (Math.abs(offset.x) > 0.5) {
    console.log('axis-lock: drift x=' + offset.x + ' y=' + offset.y);
  }

  if (!reportedVerticalMovement && Math.abs(offset.y) > 0.5) {
    reportedVerticalMovement = true;
    console.log('axis-lock: vertical moved y=' + offset.y);
  }
});

fabric.appendChild(page, content);

const rootChildren = fabric.createChildSet();

fabric.appendChildToSet(rootChildren, page);
fabric.completeRoot(surfaceId, rootChildren);

console.log('axis-lock: committed surface ' + surfaceId);
