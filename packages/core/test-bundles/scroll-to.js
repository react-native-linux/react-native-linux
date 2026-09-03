// The programmatic-scroll trace for issue #109. Three commands, each issued from the previous one's `onScroll`,
// because the event beat is the only frame boundary a bundle can see and a command is applied by the frame after
// the one that issued it:
//
//   1  scrollTo(0, 120)   moves, so exactly one onScroll reports 120
//   2  scrollTo(0, -400)  clamped to 0 before the event, so no negative offset ever reaches JavaScript
//   3  scrollTo(0, 0)     the offset it is already at, so nothing at all is emitted
//
// The no-op is last on purpose: nothing can be scheduled from its event because there is not going to be one, so
// the run ending in silence is the assertion. See *Programmatic scrolling* in docs/cpp-toolchain.md.
//
// hello_react --fabric packages/core/test-bundles/scroll-to.js

const surfaceId = 1;
const fabric = globalThis.nativeFabricUIManager;

if (fabric === undefined) {
  throw new Error('nativeFabricUIManager was not installed');
}

const handles = [];

const node = (tag, componentName, props) => {
  const handle = {};
  const created = fabric.createNode(tag, componentName, surfaceId, props, handle);

  handles.push(handle);

  return created;
};

// 150 points of viewport over 470 of content, which is the geometry scroll.js uses, reduced to the one row that
// makes it scrollable: this fixture is about the commands, and the picture is that one's job.
const scrollView = node(3, 'ScrollView', {
  position: 'absolute',
  left: 60,
  top: 60,
  width: 200,
  height: 150,
  backgroundColor: 0xff1e2430 | 0,
  onScroll: true,
  onMomentumScrollBegin: true,
  onMomentumScrollEnd: true,
});
const content = node(4, 'View', {
  position: 'absolute',
  left: 0,
  top: 0,
  width: 200,
  height: 470,
  backgroundColor: 0xff61afef | 0,
});

const scrollTo = (y) => {
  console.log('scroll-to: asking for ' + y);
  fabric.dispatchCommand(scrollView, 'scrollTo', [0, y, false]);
};

const asks = [() => scrollTo(-400), () => scrollTo(0)];

let nextAsk = 0;

fabric.registerEventHandler((instanceHandle, type, payload) => {
  const offset = payload === null || payload === undefined ? undefined : payload.contentOffset;

  console.log('scroll-to: ' + type + ' at ' + (offset === undefined ? '?' : offset.x + ',' + offset.y));

  if (type !== 'topScroll' || nextAsk >= asks.length) {
    return;
  }

  const ask = asks[nextAsk];

  nextAsk += 1;
  ask();
});

fabric.appendChild(scrollView, content);

const rootChildren = fabric.createChildSet();

fabric.appendChildToSet(rootChildren, scrollView);
fabric.completeRoot(surfaceId, rootChildren);

scrollTo(120);

console.log('scroll-to: committed surface ' + surfaceId);
