// The composition-order matrix of issue #102: where a shadow sits relative to the transform stack and the clip
// stack. Eighteen tiles on an 800x700 surface, {no transform, scale 1.5, rotate 20deg} across the columns and
// {no clip, self overflow:hidden, ancestor overflow:hidden} x {outset, inset} down the rows.
//
// Every tile is the same rounded card inside the same panel, and the only thing that varies is which of the two
// carries `overflow: hidden`, what the card's transform is, and whether the shadow is cast outwards or inwards.
// The card sits close to the panel's bottom-right corner, so an outset shadow reaches past the panel and a panel
// that clips has somewhere to cut. The card also holds a stripe wider than itself, so a card that is clipping
// says so in the picture rather than only in its shadow.
//
// What the picture has to show:
//
//   * the outset shadow scales and rotates with its card, offset and blurred in the card's own space, so the
//     scaled column's shadow is one and a half times as far out and the rotated column's shadow is a rotated
//     rounded rectangle rather than an upright one under a tilted box (core#50775, core#34320);
//   * the self-clipped rows' outset shadow still reaches past the panel even though their stripe is cut short —
//     a node's own `overflow: hidden` bounds its children and never its own outer shadow;
//   * the self-clipped rows' inset shadow stays inside the card, which is that same box seen from the inside;
//   * the ancestor-clipped rows' outset shadow stops at the panel's right and bottom edges and nowhere else.
//
// docs/cpp-toolchain.md, *Shadow composition (#102)*, describes the expected picture.

const surfaceId = 1;
const fabric = globalThis.nativeFabricUIManager;

if (fabric === undefined) {
  throw new Error('nativeFabricUIManager was not installed');
}

let nextTag = 2;

function makeNode(props, children) {
  const node = fabric.createNode(nextTag, 'View', surfaceId, props, {});

  nextTag = nextTag + 1;

  for (const child of children) {
    fabric.appendChild(node, child);
  }

  return node;
}

// A light surface and a mid-grey panel, so a black shadow reads outside the card, and a near-white card, so the
// same black reads inside it. On a dark palette the outset and the inset rows are the same picture.
const ink = 0xb3000000 | 0;
const panelFill = 0xffd7dce6 | 0;
const cardFill = 0xfffafbfc | 0;
const stripeFill = 0xffe07a3f | 0;
const surfaceFill = 0xfff2f4f8 | 0;

const columnPitch = 266;
const rowPitch = 112;
const panelWidth = 92;
const panelHeight = 64;

const outsetShadow = { offsetX: 6, offsetY: 8, blurRadius: 8, spreadDistance: 0, color: ink, inset: false };
const insetShadow = { offsetX: 6, offsetY: 8, blurRadius: 8, spreadDistance: 3, color: ink, inset: true };

const columnTransforms = [null, [{ scale: 1.5 }], [{ rotate: '20deg' }]];

// Each row is one point in the {clip} x {shadow polarity} half of the matrix.
const rows = [
  { cardClips: false, panelClips: false, shadow: outsetShadow },
  { cardClips: false, panelClips: false, shadow: insetShadow },
  { cardClips: true, panelClips: false, shadow: outsetShadow },
  { cardClips: true, panelClips: false, shadow: insetShadow },
  { cardClips: false, panelClips: true, shadow: outsetShadow },
  { cardClips: false, panelClips: true, shadow: insetShadow },
];

// Wider than the card and hanging off its left edge, so it survives a card that does not clip and is cut on both
// sides by a card that does.
function makeStripe() {
  return makeNode(
    {
      position: 'absolute',
      left: -8,
      top: 15,
      width: 68,
      height: 9,
      backgroundColor: stripeFill,
    },
    [],
  );
}

function makeCard(row, transform) {
  const props = {
    position: 'absolute',
    left: 24,
    top: 16,
    width: 56,
    height: 40,
    backgroundColor: cardFill,
    borderRadius: 12,
    boxShadow: [row.shadow],
  };

  if (row.cardClips) {
    props.overflow = 'hidden';
  }

  if (transform !== null) {
    props.transform = transform;
  }

  return makeNode(props, [makeStripe()]);
}

function makeTile(columnIndex, rowIndex) {
  const row = rows[rowIndex];
  const props = {
    position: 'absolute',
    left: columnIndex * columnPitch + 90,
    top: rowIndex * rowPitch + 20,
    width: panelWidth,
    height: panelHeight,
    backgroundColor: panelFill,
  };

  if (row.panelClips) {
    props.overflow = 'hidden';
  }

  return makeNode(props, [makeCard(row, columnTransforms[columnIndex])]);
}

const tiles = [];

for (let rowIndex = 0; rowIndex < rows.length; rowIndex = rowIndex + 1) {
  for (let columnIndex = 0; columnIndex < columnTransforms.length; columnIndex = columnIndex + 1) {
    tiles.push(makeTile(columnIndex, rowIndex));
  }
}

const surface = makeNode({ flex: 1, backgroundColor: surfaceFill }, tiles);
const rootChildren = fabric.createChildSet();

fabric.appendChildToSet(rootChildren, surface);
fabric.completeRoot(surfaceId, rootChildren);

console.log('shadow-composition: committed surface ' + surfaceId);
