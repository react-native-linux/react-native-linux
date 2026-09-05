// The shadow fixture for issue #67, one tile per way a shadow interacts with the rest of a node:
//
//   1  outset, offset down-right, blurred      the ordinary card shadow
//   2  inset                                   the well, cast inward from the top-left
//   3  two shadows in paint order              a tight dark one under a wide light one
//   4  per-corner radii                        the shadow follows the resolved rounded box, not the layout rect
//   5  under an overflow: hidden ancestor      cut where the ancestor cuts, never where the node itself would
//   6  on a rotated node                       the shadow rotates with the box it belongs to
//   7  the legacy iOS quartet                  shadowColor, shadowOffset, shadowOpacity, shadowRadius, as one drawing
//   8  spread, no blur, no offset              a hard halo, which is what spread alone is
//
// docs/cpp-toolchain.md, *Shadows (#67)*, describes the expected picture.

const surfaceId = 1;
const fabric = globalThis.nativeFabricUIManager;

if (fabric === undefined) {
  throw new Error('nativeFabricUIManager was not installed');
}

const handles = [];

// Tags count up from 2 in creation order; the tiles below are created in the order they are listed.
const view = (props, children = []) => {
  const handle = {};
  const node = fabric.createNode(2 + handles.length, 'View', surfaceId, props, handle);

  handles.push(handle);
  children.forEach((child) => fabric.appendChild(node, child));

  return node;
};

const colour = { panel: 0xff1e2430 | 0, amber: 0xffe5c07b | 0, sky: 0xff61afef | 0, ink: 0xcc000000 | 0, haze: 0x8061afef | 0 };

const card = (left, top, props, children = []) =>
  view(Object.assign({ position: 'absolute', left, top, width: 160, height: 100, backgroundColor: colour.panel }, props), children);

const shadow = (offsetX, offsetY, blurRadius, spreadDistance, color, inset = false) => ({
  offsetX,
  offsetY,
  blurRadius,
  spreadDistance,
  color,
  inset,
});

const outset = card(40, 40, { borderRadius: 16, boxShadow: [shadow(8, 8, 16, 0, colour.ink)] });
const inset = card(260, 40, { borderRadius: 16, backgroundColor: colour.amber, boxShadow: [shadow(6, 6, 12, 0, colour.ink, true)] });
const layered = card(480, 40, {
  borderRadius: 16,
  boxShadow: [shadow(0, 2, 4, 0, colour.ink), shadow(0, 12, 32, 0, colour.haze)],
});
const perCorner = card(40, 200, {
  borderTopLeftRadius: 48,
  borderBottomRightRadius: 48,
  boxShadow: [shadow(10, 10, 10, 0, colour.ink)],
});

// The clipping ancestor is 40 points shorter than the card it holds, so the card's shadow reaches past the
// ancestor's bottom edge and has to be cut there — and only there.
const clippedByAncestor = view(
  { position: 'absolute', left: 260, top: 200, width: 200, height: 100, backgroundColor: 0xff2a3142 | 0, overflow: 'hidden' },
  [card(20, 20, { borderRadius: 12, boxShadow: [shadow(0, 20, 20, 0, colour.ink)] })],
);
const rotated = card(500, 210, { borderRadius: 12, transform: [{ rotate: '15deg' }], boxShadow: [shadow(12, 12, 12, 0, colour.ink)] });
const legacy = card(40, 380, {
  borderRadius: 16,
  shadowColor: 0xff000000 | 0,
  shadowOffset: { width: 8, height: 8 },
  shadowOpacity: 0.8,
  shadowRadius: 8,
});
const halo = card(260, 380, { borderRadius: 16, boxShadow: [shadow(0, 0, 0, 10, colour.sky)] });

const surface = view({ flex: 1, backgroundColor: 0xff11141a | 0 }, [
  outset,
  inset,
  layered,
  perCorner,
  clippedByAncestor,
  rotated,
  legacy,
  halo,
]);
const rootChildren = fabric.createChildSet();

fabric.appendChildToSet(rootChildren, surface);
fabric.completeRoot(surfaceId, rootChildren);

console.log('shadow: committed surface ' + surfaceId);
