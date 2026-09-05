// The paged carousel of issue #239: a 240x150 <ScrollView pagingEnabled> over five 150-point pages, so every
// snap point is a whole viewport and the offset a flick settles at names the page it settled on.
//
// hello_react --scroll-to packages/core/test-bundles/scroll-paging.js /tmp/rnl-paging.png 180 100 4
//
// Four wheel notches are 160 points of travel, which is ten points past page two. Without paging the content
// rests at 160 and page two sits ten points high; with it the settle target is 150 and page two fills the
// viewport exactly, its corner marker in the viewport's top-left corner.

const surfaceId = 1;
const fabric = globalThis.nativeFabricUIManager;

if (fabric === undefined) {
  throw new Error('nativeFabricUIManager was not installed');
}

const pageHeight = 150;
const pageWidth = 240;
const pageColors = [0xff2f3b52 | 0, 0xff98c379 | 0, 0xffe06c75 | 0, 0xffe5c07b | 0, 0xff56b6c2 | 0];
const markerColor = 0xfff8f8f2 | 0;

const retainedHandles = [];

const make = (tag, componentName, props) => {
  const handle = { stateNode: { node: null } };
  const node = fabric.createNode(tag, componentName, surfaceId, props, handle);

  handle.stateNode.node = node;
  retainedHandles.push(handle);

  return node;
};

const carousel = make(3, 'ScrollView', {
  position: 'absolute',
  left: 60,
  top: 40,
  width: pageWidth,
  height: pageHeight,
  pagingEnabled: true,
  onScroll: true,
  onMomentumScrollBegin: true,
  onMomentumScrollEnd: true,
});

for (let page = 0; page < pageColors.length; page++) {
  const surface = make(20 + page, 'View', {
    position: 'absolute',
    left: 0,
    top: page * pageHeight,
    width: pageWidth,
    height: pageHeight,
    backgroundColor: pageColors[page],
  });

  // One marker per page, `page + 1` squares of it in a row: whichever page the carousel came to rest on can be
  // counted off the picture without reading a colour table.
  for (let square = 0; square <= page; square++) {
    fabric.appendChild(
      surface,
      make(100 + page * 10 + square, 'View', {
        position: 'absolute',
        left: 16 + square * 28,
        top: 16,
        width: 20,
        height: 20,
        backgroundColor: markerColor,
      }),
    );
  }

  fabric.appendChild(carousel, surface);
}

fabric.registerEventHandler((instanceHandle, type, payload) => {
  const offset = payload === null || payload === undefined ? undefined : payload.contentOffset;

  console.log('paging: ' + type + ' at ' + (offset === undefined ? '?' : offset.x + ',' + offset.y));
});

const rootChildren = fabric.createChildSet();

fabric.appendChildToSet(rootChildren, carousel);
fabric.completeRoot(surfaceId, rootChildren);

console.log('paging: committed surface ' + surfaceId);
