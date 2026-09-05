// The coordinate-space fixture for issue #246: one press point per box, chosen so the box's own local offset -
// `locationX/Y`, which on this platform's PointerEvent-only pipeline is the same number as `offsetX/Y` - is a
// round number a plain vector subtraction from the box's forward-mapped surface origin would get wrong. Every
// handler prints the full set: `clientX/Y` (the surface point), `pageX/Y` (the same point - this platform has no
// scrollable root, so upstream's PointerEvent always aliases the two) and `offsetX/Y` (the target-local point).
//
// hello_react --inject-pointer packages/core/test-bundles/pointer-offset.js <x> <y>

const surfaceId = 1;
const fabric = globalThis.nativeFabricUIManager;

if (fabric === undefined) {
  throw new Error('nativeFabricUIManager was not installed');
}

const namesByHandle = new Map();

const createNode = (tag, name, props) => {
  const handle = { stateNode: { node: null } };
  const node = fabric.createNode(tag, 'View', surfaceId, props, handle);

  handle.stateNode.node = node;
  namesByHandle.set(handle, name);

  return node;
};

const pointerProps = { onPointerDown: true, onPointerUp: true, onClick: true };

// A 50x50 box at (100, 100) rotated 90 degrees about its own centre (125, 125). The centre does not move under a
// rotation about itself, so pressing surface (125, 125) has to report the box's own centre - local (25, 25) - not
// whatever a plain subtraction from the rotated, forward-mapped origin would report.
const rotated = createNode(
  2,
  'rotated',
  Object.assign(
    { position: 'absolute', left: 100, top: 100, width: 50, height: 50, backgroundColor: 0xff61afef | 0 },
    pointerProps,
    { transform: [{ rotate: '90deg' }] },
  ),
);

// A 60x60 box at (300, 100) scaled 2x about its own centre (330, 130): the painted box covers surface
// (300, 100)-(360, 160). Pressing surface (300, 100) - the box's own unscaled top-left corner - is a quarter of
// the way across the painted box on both axes, a local offset of 15, not the 0 a plain subtraction from the
// forward-mapped (scaled) origin (270, 70) would report.
const scaled = createNode(
  3,
  'scaled',
  Object.assign(
    { position: 'absolute', left: 300, top: 100, width: 60, height: 60, backgroundColor: 0xffe5c07b | 0 },
    pointerProps,
    { transform: [{ scale: 2 }] },
  ),
);

// A translated ancestor (30, 20) wrapping a child scaled 1.5x about its own centre - the composition *Hit-testing
// under animation* in docs/cpp-toolchain.md describes: the ancestor's transform and the child's own compose into
// one matrix, and `pointerOffsetWithinTarget` inverts all of it in one step rather than peeling the chain apart.
const nestedChild = createNode(
  5,
  'nested-child',
  Object.assign(
    { position: 'absolute', left: 20, top: 20, width: 40, height: 40, backgroundColor: 0xff98c379 | 0 },
    pointerProps,
    { transform: [{ scale: 1.5 }] },
  ),
);
const nestedAncestor = createNode(4, 'nested-ancestor', {
  position: 'absolute',
  left: 500,
  top: 100,
  width: 80,
  height: 80,
  backgroundColor: 0xff3e4451 | 0,
  transform: [{ translateX: 30 }, { translateY: 20 }],
});

fabric.appendChild(nestedAncestor, nestedChild);

// An 80x40 box at (650, 100), opaque but `opacity: 0` (issue #50465's case) and rotated 90 degrees about its own
// centre (690, 120) (issue #299's case: invisible *and* transformed). `opacity: 0` drops it from
// `RetainedScene::snapshot` - it is not in the painted snapshot `--hit-paint-golden` samples - but it is still in
// the retained scene the hit test walks, and `SceneHit` still carries the matrix `hitTestNode` composed for it,
// because that walk does not filter by visibility either. The centre does not move under a rotation about
// itself, so pressing surface (690, 120) reports the box's own centre - local (40, 20) - proving the offset came
// from this node's own matrix rather than the identity fallback a transform-less invisible node would also pass
// under.
const invisible = createNode(
  6,
  'invisible',
  Object.assign(
    { position: 'absolute', left: 650, top: 100, width: 80, height: 40, backgroundColor: 0xffffffff | 0, opacity: 0 },
    pointerProps,
    { transform: [{ rotate: '90deg' }] },
  ),
);

fabric.registerEventHandler((instanceHandle, type, payload) => {
  const target = namesByHandle.get(instanceHandle) ?? 'unknown';

  console.log(
    'pointer-offset: ' +
      type +
      ' on ' +
      target +
      ' client=' +
      payload.clientX +
      ',' +
      payload.clientY +
      ' page=' +
      payload.pageX +
      ',' +
      payload.pageY +
      ' offset=' +
      payload.offsetX +
      ',' +
      payload.offsetY,
  );
});

const container = createNode(7, 'container', { flex: 1 });

fabric.appendChild(container, rotated);
fabric.appendChild(container, scaled);
fabric.appendChild(container, nestedAncestor);
fabric.appendChild(container, invisible);

const rootChildren = fabric.createChildSet();

fabric.appendChildToSet(rootChildren, container);
fabric.completeRoot(surfaceId, rootChildren);

console.log('pointer-offset: committed surface ' + surfaceId);
