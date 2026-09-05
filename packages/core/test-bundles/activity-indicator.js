// The <ActivityIndicator> fixture for issue #261, one tile per state the control can be in:
//
//   1  small, animating                     the arc at the angle the golden's frame count reaches
//   2  large, animating                     the same angle, with the thicker stroke `size` picks
//   3  small, animating, custom colour      `color`, which is null on this platform until an app names one
//   4  stopped, hidesWhenStopped            nothing at all, over the panel that proves the tile is there
//   5  stopped, hidesWhenStopped: false     the arc, frozen where it stopped
//
// The box is the style, exactly as ActivityIndicator.js sets it: 20 points for `small` and 36 for `large`.
// docs/cpp-toolchain.md, *ActivityIndicator (#261)*, describes the expected picture.

const surfaceId = 1;
const fabric = globalThis.nativeFabricUIManager;

if (fabric === undefined) {
  throw new Error('nativeFabricUIManager was not installed');
}

const handles = [];

const node = (componentName, props) => {
  const handle = {};
  const created = fabric.createNode(2 + handles.length, componentName, surfaceId, props, handle);

  handles.push(handle);

  return created;
};

const colour = { panel: 0xff1e2430 | 0, sky: 0xff61afef | 0 };

const panel = (left, top) =>
  node('View', { position: 'absolute', left: left - 20, top: top - 20, width: 76, height: 76, backgroundColor: colour.panel });

const indicator = (left, top, size, props) =>
  node(
    'ActivityIndicatorView',
    Object.assign({ position: 'absolute', left, top, width: size, height: size }, props),
  );

const container = node('View', { flex: 1 });

const children = [
  panel(80, 80),
  indicator(80, 80, 20, { animating: true, size: 'small' }),
  panel(260, 80),
  indicator(260, 80, 36, { animating: true, size: 'large' }),
  panel(440, 80),
  indicator(440, 80, 36, { animating: true, size: 'large', color: colour.sky }),
  panel(80, 260),
  indicator(80, 260, 36, { animating: false, hidesWhenStopped: true, size: 'large' }),
  panel(260, 260),
  indicator(260, 260, 36, { animating: false, hidesWhenStopped: false, size: 'large' }),
];

children.forEach((child) => fabric.appendChild(container, child));

const rootChildren = fabric.createChildSet();

fabric.appendChildToSet(rootChildren, container);
fabric.completeRoot(surfaceId, rootChildren);

console.log('activity-indicator: committed surface ' + surfaceId);
