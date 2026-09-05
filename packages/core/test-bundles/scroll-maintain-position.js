// The maintainVisibleContentPosition fixture for issue #240, and core#58186: a chat-style log that grows at the
// top while the user is reading the middle of it. Without the prop the prepend pushes everything down by the
// height of what arrived; with it the offset moves by the same amount in the same commit and the row the user was
// looking at is painted exactly where it was.
//
// hello_react --maintain-position-golden packages/core/test-bundles/scroll-maintain-position.js /tmp/rnl-maintain-position.png 160 100 3
//
// Three wheel notches is 120 points, which leaves the green row cut off at the top edge of the viewport. The two
// rows the timer prepends are 160 points of content above it, so the offset has to end up at 280 for that row to
// still be there.

const surfaceId = 1;
const fabric = globalThis.nativeFabricUIManager;

if (fabric === undefined) {
  throw new Error('nativeFabricUIManager was not installed');
}

// PointerEventsProcessor and the event target both resolve back to a shadow node through the instance handle, and
// C++ holds it weakly, so every handle is retained here for the same reason React retains them on its fibers.
const handles = [];

const createNode = (tag, componentName, props) => {
  const handle = { stateNode: { node: null } };
  const node = fabric.createNode(tag, componentName, surfaceId, props, handle);

  handle.stateNode.node = node;
  handles.push(handle);

  return node;
};

const rowProps = (backgroundColor) => ({ width: 200, height: 70, marginBottom: 10, backgroundColor });

const scrollView = createNode(2, 'ScrollView', {
  position: 'absolute',
  left: 60,
  top: 60,
  width: 200,
  height: 150,
  backgroundColor: 0xff1e2430 | 0,
  maintainVisibleContentPosition: { minIndexForVisible: 0 },
  onScroll: true,
  onMomentumScrollBegin: true,
  onMomentumScrollEnd: true,
});

// The content container React Native's <ScrollView> always renders around its children, and the node whose
// children the anchor is chosen from on every platform that implements this prop.
const content = createNode(3, 'View', { width: 200 });

const rows = [0xffe06c75, 0xff98c379, 0xff61afef, 0xffe5c07b, 0xffc678dd, 0xff56b6c2].map((backgroundColor, index) =>
  createNode(10 + index, 'View', rowProps(backgroundColor | 0)),
);

// Directly below the viewport, as in scroll.js: content that overflowed the clip would land on it.
const marker = createNode(4, 'View', {
  position: 'absolute',
  left: 60,
  top: 240,
  width: 200,
  height: 40,
  backgroundColor: 0xff3366cc | 0,
});

const container = createNode(5, 'View', { flex: 1 });

rows.forEach((row) => fabric.appendChild(content, row));
fabric.appendChild(scrollView, content);
fabric.appendChild(container, scrollView);
fabric.appendChild(container, marker);

const firstChildren = fabric.createChildSet();

fabric.appendChildToSet(firstChildren, container);
fabric.completeRoot(surfaceId, firstChildren);

// The two messages that arrive while the user is reading, prepended above everything already in the log. They
// commit as their own mounting transaction, which is the transaction the offset has to be adjusted in.
const prepend = () => {
  const arrived = [0xff2a3142, 0xff3a4152].map((backgroundColor, index) =>
    createNode(20 + index, 'View', rowProps(backgroundColor | 0)),
  );
  const nextContent = fabric.cloneNodeWithNewChildren(content);

  arrived.forEach((row) => fabric.appendChild(nextContent, row));
  rows.forEach((row) => fabric.appendChild(nextContent, row));

  const nextScrollView = fabric.cloneNodeWithNewChildren(scrollView);

  fabric.appendChild(nextScrollView, nextContent);

  const nextContainer = fabric.cloneNodeWithNewChildren(container);

  fabric.appendChild(nextContainer, nextScrollView);
  fabric.appendChild(nextContainer, marker);

  const secondChildren = fabric.createChildSet();

  fabric.appendChildToSet(secondChildren, nextContainer);
  fabric.completeRoot(surfaceId, secondChildren);

  console.log('maintain-position: prepended two rows');

  // Long after the adjustment, so a controller that re-applied the delta on every frame rather than on the commit
  // that earned it would report an offset that kept growing instead of this one.
  setTimeout(() => {
    console.log('maintain-position: settled at ' + lastOffsetText);
  }, 500);
};

let lastOffsetText = '?';

let hasArmedPrepend = false;

// Armed by the first scroll event rather than at load, so the prepend lands a known interval after the scroll has
// been reported rather than a guessed one after the bundle started.
fabric.registerEventHandler((instanceHandle, type, payload) => {
  const offset = payload.contentOffset;

  if (offset !== undefined) {
    lastOffsetText = offset.x + ',' + offset.y;
  }

  console.log('maintain-position: ' + type + ' at ' + lastOffsetText);

  if (type === 'topScroll' && !hasArmedPrepend) {
    hasArmedPrepend = true;
    setTimeout(prepend, 250);
  }
});

console.log('maintain-position: committed surface ' + surfaceId);
