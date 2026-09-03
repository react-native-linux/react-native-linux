// The hit-versus-paint fixture, issue #35. Seven cases, each one a way the picture and the click can come apart,
// and every node paints a colour no other node paints — that is what lets `--hit-paint-golden` read a pixel, name
// the node that painted it, and compare it against the node the scene's own hit test answers at the same point.
//
// The cases, left to right and top to bottom:
//
//   1  two overlapping siblings, equal frames        the later child is on top and is what a press finds
//   2  the same pair with zIndex reversing them      the press follows zIndex, not document order
//   3  a child translated out from under its slot    the vacated point is the parent, the new one is the child
//   4  rotate about three transformOrigins           the hit region rotates with the painted region
//   5  scale about the same origin the paint uses    (rn-macos#2147: pixels move, hitbox does not)
//   6  a child clipped by a rounded overflow: hidden the corner outside the clip is the parent
//   7  nested transformed ancestors                  composed, not last-wins
//
// docs/cpp-toolchain.md, *Hit-testing agrees with the picture*, describes the expected picture.
//
// hello_react --hit-paint-golden packages/core/test-bundles/hit-paint.js /tmp/hit-paint.png

const surfaceId = 1;
const fabric = globalThis.nativeFabricUIManager;

if (fabric === undefined) {
  throw new Error('nativeFabricUIManager was not installed');
}

// The handles are kept alive for the reason every fixture keeps them: C++ holds them weakly. `stateNode.node` is
// the shape PointerEventsProcessor resolves a target through, and `names` is what turns a press back into a
// readable trace line, so the same fixture proves the agreement in pixels and in events.
const instanceHandles = [];
const names = new Map();

const createView = (name, props, ...children) => {
  const handle = { stateNode: { node: null } };
  const node = fabric.createNode(instanceHandles.length + 2, 'View', surfaceId, props, handle);

  handle.stateNode.node = node;
  instanceHandles.push(handle);
  names.set(handle, name);

  for (const child of children) {
    fabric.appendChild(node, child);
  }

  return node;
};

const pointerProps = { onPointerDown: true, onPointerUp: true, onClick: true };

// One colour per node, and never twice: a repeated colour would make a pixel ambiguous and the proof would refuse
// to run. The surface itself is the first of them, because every pixel has to belong to some node.
const surfaceColor = 0xff11141a | 0;
const box = (name, left, top, width, height, backgroundColor, props = {}, ...children) =>
  createView(
    name,
    Object.assign({ position: 'absolute', left, top, width, height, backgroundColor }, pointerProps, props),
    ...children,
  );

// 1. Two siblings with the same frame. Document order is paint order, so the second one is on top.
const overlapUnder = box('overlap-under', 30, 30, 150, 100, 0xffe06c75 | 0);
const overlapOver = box('overlap-over', 70, 55, 150, 100, 0xff61afef | 0);

// 2. The same pair, with zIndex putting the *first* child on top. Fabric sorts the children by orderIndex before
// the mounting layer ever sees them, so this is the assertion that the sort reaches the hit test as well.
const zIndexRaised = box('z-raised', 280, 30, 150, 100, 0xff98c379 | 0, { zIndex: 2 });
const zIndexLowered = box('z-lowered', 320, 55, 150, 100, 0xffc678dd | 0, { zIndex: 1 });

// 3. A child translated 90 points right of the slot it was laid out in. The slot is the parent's colour and the
// new location is the child's — rn-macos#2147 is the version where only the pixels move.
const translatedParent = box(
  'translate-parent',
  530,
  30,
  230,
  120,
  0xff56b6c2 | 0,
  {},
  box('translate-child', 0, 20, 90, 80, 0xffe5c07b | 0, { transform: [{ translateX: 90 }] }),
);

// 4. The same square rotated about three different origins, so a hit region that ignored `transformOrigin` would
// land on the untransformed square in at least two of the three.
const rotatedAboutCenter = box('rotate-centre', 60, 200, 100, 100, 0xffd19a66 | 0, { transform: [{ rotate: '25deg' }] });
const rotatedAboutTopLeft = box('rotate-top-left', 200, 200, 100, 100, 0xff2fa4a0 | 0, {
  transform: [{ rotate: '25deg' }],
  transformOrigin: 'top left',
});
const rotatedAboutBottomRight = box('rotate-bottom-right', 340, 200, 100, 100, 0xff8a7fd4 | 0, {
  transform: [{ rotate: '-25deg' }],
  transformOrigin: 'bottom right',
});

// 5. Scaled about its own centre: the painted square is twice the laid-out one and so is what can be pressed.
const scaled = box('scaled', 520, 215, 60, 60, 0xffb4d273 | 0, { transform: [{ scale: 2 }] });

// 6. A rounded `overflow: hidden` parent with a square child covering it. Every pixel of the child's corners is
// cut by the parent's arc, and so is every press there.
const clippedParent = box(
  'clip-parent',
  650,
  190,
  120,
  110,
  0xff3d4a5c | 0,
  { borderRadius: 40, overflow: 'hidden' },
  box('clip-child', 0, 0, 120, 110, 0xffef8f5a | 0),
);

// 7. Nested transforms: the outer node translates and the inner one scales inside it. Composed, the inner square
// is drawn at the outer translation *and* at the inner scale; last-wins would put it in neither place.
const nestedTransforms = box(
  'nested-outer',
  60,
  360,
  260,
  180,
  0xff7f5f9e | 0,
  { transform: [{ translateX: 40 }, { translateY: 10 }] },
  box('nested-inner', 20, 20, 80, 80, 0xff6fa8dc | 0, { transform: [{ scale: 1.5 }] }),
);

// A plain reference square that nothing transforms, so a run that broke every transform at once still has one
// node whose agreement means something.
const untransformed = box('untransformed', 400, 380, 120, 120, 0xffcf6a87 | 0);

const surface = createView(
  'surface',
  Object.assign({ flex: 1, backgroundColor: surfaceColor }, pointerProps),
  overlapUnder,
  overlapOver,
  zIndexRaised,
  zIndexLowered,
  translatedParent,
  rotatedAboutCenter,
  rotatedAboutTopLeft,
  rotatedAboutBottomRight,
  scaled,
  clippedParent,
  nestedTransforms,
  untransformed,
);
fabric.registerEventHandler((instanceHandle, type, payload) => {
  console.log(
    'hit-paint: ' + type + ' on ' + (names.get(instanceHandle) ?? 'unknown') + ' at ' +
      payload.clientX + ',' + payload.clientY,
  );
});

const rootChildren = fabric.createChildSet();

fabric.appendChildToSet(rootChildren, surface);
fabric.completeRoot(surfaceId, rootChildren);

console.log('hit-paint: committed surface ' + surfaceId);
