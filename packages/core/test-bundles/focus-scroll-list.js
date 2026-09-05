// The scroll-into-view fixture for issue #248: a ScrollView 150 points tall over 350 points of content, five
// stacked focusable rows 70 points apiece, the last of which — `row4`, tag 14 — starts at content y=280 and is
// therefore entirely below the viewport at rest.
//
// hello_react --fabric packages/core/test-bundles/focus-scroll-list.js
// hello_react --focus-command-golden packages/core/test-bundles/focus-scroll-list.js /tmp/rnl-focus-command.png 14
//
// With nothing focused, Shift+Tab starts at the last focusable — `row4` — so a single Shift+Tab press is the
// whole of the reverse-traversal scroll-into-view proof: it has to both focus `row4` and bring it on screen. The
// same row's tag is also `--focus-command-golden`'s target, for the programmatic-`focus()` half of the same
// proof: neither this bundle nor the harness dispatches that command, `FabricHost::injectFocusCommand` does.

const surfaceId = 1;
const fabric = globalThis.nativeFabricUIManager;

if (fabric === undefined) {
  throw new Error('nativeFabricUIManager was not installed');
}

// Nothing here targets a pointer through `PointerEventsProcessor`, so the handle carries only what the trace
// below reads back out of it: the row's own name.
const createNode = (tag, name, componentName, props) => fabric.createNode(tag, componentName, surfaceId, props, { name });

const container = createNode(2, 'container', 'View', { flex: 1 });

const scrollView = createNode(3, 'scrollView', 'ScrollView', {
  position: 'absolute',
  left: 60,
  top: 60,
  width: 200,
  height: 150,
  backgroundColor: 0xff1e2430 | 0,
  onScroll: true,
});

const rowColors = [0xffe06c75 | 0, 0xff98c379 | 0, 0xff61afef | 0, 0xffe5c07b | 0, 0xffc678dd | 0];

rowColors.forEach((backgroundColor, index) => {
  const row = createNode(10 + index, 'row' + index, 'View', {
    position: 'absolute',
    left: 0,
    top: index * 70,
    width: 200,
    height: 60,
    backgroundColor,
    accessible: true,
  });

  fabric.appendChild(scrollView, row);
});

fabric.appendChild(container, scrollView);

const rootChildren = fabric.createChildSet();

fabric.appendChildToSet(rootChildren, container);
fabric.completeRoot(surfaceId, rootChildren);

fabric.registerEventHandler((instanceHandle, type, payload) => {
  const offset = payload === null || payload === undefined ? undefined : payload.contentOffset;
  const name = instanceHandle === null || instanceHandle === undefined ? 'unknown' : instanceHandle.name;

  console.log('focus-scroll-list: ' + type + ' on ' + name + (offset === undefined ? '' : ' at ' + offset.x + ',' + offset.y));
});

console.log('focus-scroll-list: committed surface ' + surfaceId);
