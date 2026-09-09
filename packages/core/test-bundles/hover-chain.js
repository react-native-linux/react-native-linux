// The hover-chain conformance fixture for issue #36. `PointerEventsProcessor` — vendored from upstream, unmodified
// — is what turns the raw `topPointerMove` this platform's `InputDispatcher` sends at whatever hit-testing found
// into `pointerEnter`/`pointerLeave`/`pointerOver`/`pointerOut`; see `PointerRouter`'s own docblock in
// `InputPipeline.h` for why that logic is not duplicated here. This bundle is the end-to-end proof: a real Hermes
// bundle, the real `UIManagerBinding`, the real processor, under `--inject-pointer`.
//
// hello_react --inject-pointer packages/core/test-bundles/hover-chain.js
//
// Layout, left to right along y=50:
//   left      (0,   0, 100, 100) — a plain sibling.
//   right     (100, 0, 100, 100) — a sibling holding `nested`, a 30x30 box at its own (10, 10).
//   covered   (300, 0, 100, 100) — under `cover`, a same-sized sibling committed after it with `zIndex: 1`, so a
//             point inside both always resolves to `cover` (issue #36's zIndex case).
//   clipped   (500, 0, 100, 100), `overflow: hidden` — holds `poking`, a 60x20 box at its own (80, 10), so only
//             its left 20 pixels (surface 580-600) are inside `clipped`'s frame; a point past 600 is outside the
//             clip and is not `poking` at all (issue #36's overflow case).

const surfaceId = 1;
const fabric = globalThis.nativeFabricUIManager;

if (fabric === undefined) {
  throw new Error('nativeFabricUIManager was not installed');
}

const hoverProps = {
  onPointerEnter: true,
  onPointerLeave: true,
  onPointerOver: true,
  onPointerOut: true,
  onPointerMove: true,
};

// Named on the handle itself rather than through a side table — `registerEventHandler` hands the handle straight
// back, and reading `boxName` off it is the whole of what this fixture needs a name for.
const boxAt = (tag, boxName, left, top, width, height, backgroundColor, extraProps) => {
  const handle = { boxName, stateNode: { node: null } };
  const props = Object.assign(
    { position: 'absolute', left, top, width, height, backgroundColor },
    hoverProps,
    extraProps ?? {},
  );

  handle.stateNode.node = fabric.createNode(tag, 'View', surfaceId, props, handle);

  return handle.stateNode.node;
};

const left = boxAt(2, 'left', 0, 0, 100, 100, 0xff61afef | 0);
const right = boxAt(3, 'right', 100, 0, 100, 100, 0xffe5c07b | 0);
const nested = boxAt(4, 'nested', 10, 10, 30, 30, 0xff98c379 | 0);

fabric.appendChild(right, nested);

const covered = boxAt(5, 'covered', 300, 0, 100, 100, 0xffe06c75 | 0);
const cover = boxAt(6, 'cover', 300, 0, 100, 100, 0xffc678dd | 0, { zIndex: 1 });

const clipped = boxAt(7, 'clipped', 500, 0, 100, 100, 0xff3e4451 | 0, { overflow: 'hidden' });
const poking = boxAt(8, 'poking', 80, 10, 60, 20, 0xff56b6c2 | 0);

fabric.appendChild(clipped, poking);

fabric.registerEventHandler((instanceHandle, type, payload) => {
  const target = instanceHandle.boxName ?? 'unknown';

  console.log('hover-chain: ' + type + ' on ' + target + ' at ' + payload.clientX + ',' + payload.clientY);
});

// Appended straight to the root rather than wrapped in a `flex: 1` container, which would cover the whole window
// and leave no point that hit-tests to the bare root at all — issue #36's case 3 needs one.
const rootChildren = fabric.createChildSet();

fabric.appendChildToSet(rootChildren, left);
fabric.appendChildToSet(rootChildren, right);
fabric.appendChildToSet(rootChildren, covered);
fabric.appendChildToSet(rootChildren, cover);
fabric.appendChildToSet(rootChildren, clipped);
fabric.completeRoot(surfaceId, rootChildren);

console.log('hover-chain: committed surface ' + surfaceId);
