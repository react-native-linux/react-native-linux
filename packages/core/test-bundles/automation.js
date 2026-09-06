// The fixture the automation channel of issue #214 is asked about. Every node is absolutely positioned with a
// fixed size, because the visual-tree snapshot the driver compares against is checked in: anything laid out
// against the surface would carry the headless compositor's output size into the snapshot and have to be
// re-blessed whenever the rig changed size. The tags are written out for the same reason.
//
// It also calls globalThis.__rnlMarkTestPassed(), which is the whole of the headless assertion protocol: a logic
// test asserts in JavaScript and says so, and MarkTestPassed reports it without a screenshot.
//
// rnl_window --automation --fabric packages/core/test-bundles/automation.js

const surfaceId = 1;
const fabric = globalThis.nativeFabricUIManager;

if (fabric === undefined) {
  throw new Error('nativeFabricUIManager was not installed');
}

// C++ holds instance handles weakly, so React retains them on its fibers. Keep them alive for the same reason.
const instanceHandles = [{}, {}, {}, {}];

const panel = fabric.createNode(
  2,
  'View',
  surfaceId,
  {
    position: 'absolute',
    left: 40,
    top: 40,
    width: 240,
    height: 120,
    backgroundColor: 0xff1e2430 | 0,
    testID: 'panel',
    accessibilityLabel: 'Automation panel',
  },
  instanceHandles[0],
);

const box = fabric.createNode(
  3,
  'View',
  surfaceId,
  {
    position: 'absolute',
    left: 16,
    top: 16,
    width: 80,
    height: 40,
    backgroundColor: 0xff3366cc | 0,
    testID: 'box',
  },
  instanceHandles[1],
);

const caption = fabric.createNode(
  4,
  'Paragraph',
  surfaceId,
  { position: 'absolute', left: 16, top: 72, width: 200, height: 24, color: 0xfff2f4f8 | 0, fontSize: 16 },
  instanceHandles[2],
);

fabric.appendChild(caption, fabric.createNode(5, 'RawText', surfaceId, { text: 'Automation' }, instanceHandles[3]));
fabric.appendChild(panel, box);
fabric.appendChild(panel, caption);

const rootChildren = fabric.createChildSet();

fabric.appendChildToSet(rootChildren, panel);
fabric.completeRoot(surfaceId, rootChildren);

globalThis.__rnlMarkTestPassed();

console.log('automation: committed surface 1');
