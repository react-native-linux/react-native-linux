// The <ScrollView> fixture for issue #16: one 200x150 viewport holding 470 points of content, plus a marker view
// below it that nothing inside the viewport may ever paint over.
//
// hello_react --fabric packages/core/test-bundles/scroll.js
// hello_react --scroll-to packages/core/test-bundles/scroll.js /tmp/rnl-scroll.png 160 100 3
//
// Three wheel notches are 120 points of travel, so the golden shows the content offset by exactly that: the second
// row cut off at the top edge of the viewport, the fourth cut off at the bottom edge, and the marker untouched.

const surfaceId = 1;
const fabric = globalThis.nativeFabricUIManager;

if (fabric === undefined) {
  throw new Error('nativeFabricUIManager was not installed');
}

// PointerEventsProcessor and the event target both resolve back to a shadow node through the instance handle, and
// C++ holds it weakly, so every handle is retained here for the same reason React retains them on its fibers.
const createInstanceHandle = () => ({ stateNode: { node: null } });
const handles = [];

const createNode = (tag, componentName, props) => {
  const handle = createInstanceHandle();
  const node = fabric.createNode(tag, componentName, surfaceId, props, handle);

  handle.stateNode.node = node;
  handles.push(handle);

  return node;
};

const container = createNode(2, 'View', { flex: 1 });

const scrollView = createNode(3, 'ScrollView', {
  position: 'absolute',
  left: 60,
  top: 60,
  width: 200,
  height: 150,
  backgroundColor: 0xff1e2430 | 0,
  onScroll: true,
  onScrollBeginDrag: true,
  onScrollEndDrag: true,
  onMomentumScrollBegin: true,
  onMomentumScrollEnd: true,
});

const rowColors = [
  0xffe06c75 | 0,
  0xff98c379 | 0,
  0xff61afef | 0,
  0xffe5c07b | 0,
  0xffc678dd | 0,
  0xff56b6c2 | 0,
];

rowColors.forEach((backgroundColor, index) => {
  const row = createNode(10 + index, 'View', {
    position: 'absolute',
    left: 0,
    top: index * 80,
    width: 200,
    height: 70,
    backgroundColor,
  });

  fabric.appendChild(scrollView, row);
});

// Directly below the viewport. Clipped content that leaked would land on or beside it, so a golden in which this
// rectangle is anything other than a clean 200x40 block is a broken clip.
const marker = createNode(4, 'View', {
  position: 'absolute',
  left: 60,
  top: 240,
  width: 200,
  height: 40,
  backgroundColor: 0xff3366cc | 0,
});

fabric.registerEventHandler((instanceHandle, type, payload) => {
  const offset = payload.contentOffset;

  console.log('scroll: ' + type + ' at ' + (offset === undefined ? '?' : offset.x + ',' + offset.y));
});

fabric.appendChild(container, scrollView);
fabric.appendChild(container, marker);

const rootChildren = fabric.createChildSet();

fabric.appendChildToSet(rootChildren, container);
fabric.completeRoot(surfaceId, rootChildren);

console.log('scroll: committed surface ' + surfaceId);
