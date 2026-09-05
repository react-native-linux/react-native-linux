// The `contentInset` fixture of issue #241: the same 200x150 viewport over 470 points of content that scroll.js
// uses, with 50 points of inset above the content and 30 below it, and a `contentOffset` that starts the run at
// the top of that top inset rather than at the top of the content.
//
// hello_react --golden packages/core/test-bundles/scroll-inset.js /tmp/rnl-scroll-inset.png
//
// The range is therefore [-50, 350] rather than [0, 320], and the golden is the start of it: 50 points of bare
// panel above the first row, which is the inset the content was pushed down by, and the marker below the
// viewport untouched. Ten wheel notches are 400 points, which is exactly the whole range, so the e2e scenario
// ends at 350 — the content's own end, 320, plus the 30 points of inset under it.

const surfaceId = 1;
const fabric = globalThis.nativeFabricUIManager;

if (fabric === undefined) {
  throw new Error('nativeFabricUIManager was not installed');
}

const namesByHandle = new Map();

const make = (tag, componentName, name, props) => {
  const handle = { stateNode: { node: null } };

  handle.stateNode.node = fabric.createNode(tag, componentName, surfaceId, props, handle);
  namesByHandle.set(handle, name);

  return handle.stateNode.node;
};

const container = make(2, 'View', 'container', { flex: 1 });

const list = make(3, 'ScrollView', 'list', {
  position: 'absolute',
  left: 60,
  top: 60,
  width: 200,
  height: 150,
  backgroundColor: 0xff1e2430 | 0,
  contentInset: { top: 50, bottom: 30 },
  contentOffset: { x: 0, y: -50 },
  // The same 400 points of travel on the fast curve, which comes to rest inside the run rather than after it.
  decelerationRate: 0.99,
  onScroll: true,
  onMomentumScrollEnd: true,
  onClick: true,
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
  fabric.appendChild(
    list,
    make(10 + index, 'View', 'row' + index, {
      position: 'absolute',
      left: 0,
      top: index * 80,
      width: 200,
      height: 70,
      backgroundColor,
      onClick: true,
    }),
  );
});

// Directly below the viewport, exactly as in scroll.js: content that leaked past the clip would land on or
// beside it, so anything other than a clean 200x40 block is a broken clip.
const marker = make(4, 'View', 'marker', {
  position: 'absolute',
  left: 60,
  top: 240,
  width: 200,
  height: 40,
  backgroundColor: 0xff3366cc | 0,
});

fabric.registerEventHandler((instanceHandle, type, payload) => {
  const name = namesByHandle.get(instanceHandle);
  const target = name === undefined ? 'unknown' : name;

  if (type === 'topClick') {
    console.log('inset: topClick on ' + target);

    return;
  }

  const offset = payload === null || payload === undefined ? undefined : payload.contentOffset;

  console.log('inset: ' + type + ' at ' + (offset === undefined ? '?' : offset.x + ',' + offset.y));
});

fabric.appendChild(container, list);
fabric.appendChild(container, marker);

const rootChildren = fabric.createChildSet();

fabric.appendChildToSet(rootChildren, container);
fabric.completeRoot(surfaceId, rootChildren);

console.log('inset: committed surface ' + surfaceId);
