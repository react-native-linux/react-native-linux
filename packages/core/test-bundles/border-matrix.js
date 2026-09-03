// The border-painting matrix for the golden-image rig, issue #100. Five rows, top to bottom:
//
//   A  uniform:              square and rounded rings, one side's width alone on a rounded box, a translucent
//                            ring over an opaque fill, and a translucent ring on a rounded box.
//   B  per side, three scales: four different side colours at 1x, 1.5x and 2x — the corner mitres are the seam
//                            case, and a seam is a scale-dependent artifact, so the same tile is drawn three
//                            times at three sizes.
//   C  transparent and unset: one transparent side must not erase the other three, and a border with no colour
//                            at all draws nothing on this platform. See docs/cpp-toolchain.md.
//   D  hairlines:            sub-pixel widths, which have to be drawn rather than rounded away.
//   E  large radius:         mitres that meet on an arc rather than on a straight edge.
//
// docs/cpp-toolchain.md, *View props fidelity*, describes the expected picture.

const surfaceId = 1;
const fabric = globalThis.nativeFabricUIManager;

if (fabric === undefined) {
  throw new Error('nativeFabricUIManager was not installed');
}

// C++ holds instance handles weakly, so React retains them on its fibers. Keep them alive for the same reason.
const instanceHandles = [];

const createView = (props) => {
  const handle = {};
  const node = fabric.createNode(instanceHandles.length + 2, 'View', surfaceId, props, handle);

  instanceHandles.push(handle);

  return node;
};

const color = {
  amber: 0xffe5c07b | 0,
  cyan: 0xff56b6c2 | 0,
  green: 0xff98c379 | 0,
  halfGreen: 0x8098c379 | 0,
  halfSky: 0x8061afef | 0,
  panel: 0xff1e2430 | 0,
  purple: 0xffc678dd | 0,
  red: 0xffe06c75 | 0,
  sky: 0xff61afef | 0,
  transparent: 0x00000000 | 0,
  white: 0xfff2f4f8 | 0,
};

const tile = (left, top, width, height, props) =>
  createView(Object.assign({ position: 'absolute', left, top, width, height, backgroundColor: color.panel }, props));

const perSideColors = {
  borderWidth: 10,
  borderRadius: 24,
  borderTopColor: color.green,
  borderRightColor: color.sky,
  borderBottomColor: color.purple,
  borderLeftColor: color.red,
};

const uniformSquare = tile(30, 16, 120, 80, { borderWidth: 8, borderColor: color.amber });
const uniformRounded = tile(180, 16, 120, 80, { borderRadius: 26, borderWidth: 8, borderColor: color.amber });
// core#37954: a radius plus one side's width only. The other three sides keep the box, the bottom draws.
const bottomOnly = tile(330, 16, 120, 80, { borderRadius: 26, borderBottomWidth: 14, borderColor: color.red });
// core#39286: a translucent ring composites against the opaque fill under it, not against a fresh layer.
const translucentOverFill = tile(480, 16, 120, 80, { backgroundColor: color.cyan, borderWidth: 12, borderColor: color.halfGreen });
// core#49606: opacity and radius together still draw.
const translucentRounded = tile(630, 16, 120, 80, { borderRadius: 26, borderWidth: 12, borderColor: color.halfSky });

const perSideAtOneX = tile(40, 170, 120, 90, perSideColors);
const perSideAtOneAndAHalfX = tile(
  250,
  170,
  120,
  90,
  Object.assign({ transform: [{ scale: 1.5 }] }, perSideColors),
);
const perSideAtTwoX = tile(520, 170, 120, 90, Object.assign({ transform: [{ scale: 2 }] }, perSideColors));

// core#34722: one transparent side must not take the other three with it.
const transparentTop = tile(30, 320, 120, 80, {
  borderWidth: 10,
  borderTopColor: color.transparent,
  borderRightColor: color.sky,
  borderBottomColor: color.purple,
  borderLeftColor: color.red,
});
const transparentTopRounded = tile(180, 320, 120, 80, {
  borderRadius: 22,
  borderWidth: 10,
  borderTopColor: color.transparent,
  borderRightColor: color.sky,
  borderBottomColor: color.purple,
  borderLeftColor: color.red,
});
// An unset colour and an explicitly transparent one are the same value in upstream's cxx `Color`, so these two
// tiles are identical and both draw only their fill. That is the deviation *View props fidelity* records.
const unsetColor = tile(330, 320, 120, 80, { borderWidth: 10 });
const allTransparent = tile(480, 320, 120, 80, { borderWidth: 10, borderColor: color.transparent });
// Zero width beside a set width: only the top is a border, and it keeps its colour.
const oneSideOnly = tile(630, 320, 120, 80, { borderRadius: 22, borderTopWidth: 16, borderTopColor: color.cyan });

// core#58054: every one of these has to be visible. At this rig's scale they all resolve to one device pixel.
const quarterHairline = tile(30, 415, 120, 60, { borderWidth: 0.25, borderColor: color.white });
const halfHairline = tile(180, 415, 120, 60, { borderWidth: 0.5, borderColor: color.white });
const threeQuarterHairline = tile(330, 415, 120, 60, { borderWidth: 0.75, borderColor: color.white });
const onePointHairline = tile(480, 415, 120, 60, { borderWidth: 1, borderColor: color.white });
const roundedHairline = tile(630, 415, 120, 60, { borderRadius: 20, borderWidth: 0.4, borderColor: color.white });

// Issue #101: dashed and dotted, square and rounded, at one width and at four; a dashed border over a background
// that must survive it (core#42289); and a four-colour dashed ring, where one phase runs around all four sides.
const dashedSquare = tile(30, 585, 120, 70, { borderWidth: 4, borderColor: color.amber, borderStyle: 'dashed' });
const dashedRounded = tile(180, 585, 120, 70, {
  borderRadius: 24,
  borderWidth: 4,
  borderColor: color.amber,
  borderStyle: 'dashed',
});
const dottedRounded = tile(330, 585, 120, 70, {
  borderRadius: 24,
  borderWidth: 6,
  borderColor: color.sky,
  borderStyle: 'dotted',
});
const dashedOverFill = tile(480, 585, 120, 70, {
  backgroundColor: color.cyan,
  borderRadius: 24,
  borderWidth: 5,
  borderColor: color.purple,
  borderStyle: 'dashed',
});
const dashedPerSide = tile(630, 585, 120, 70, Object.assign({}, perSideColors, { borderStyle: 'dashed' }));

const clampedRing = tile(30, 495, 180, 85, { backgroundColor: color.cyan, borderRadius: 200, borderWidth: 14, borderColor: color.purple });
const largeRadiusPerSideWidths = tile(240, 495, 180, 85, {
  borderRadius: 40,
  borderTopWidth: 20,
  borderRightWidth: 4,
  borderBottomWidth: 20,
  borderLeftWidth: 4,
  borderTopColor: color.green,
  borderRightColor: color.sky,
  borderBottomColor: color.purple,
  borderLeftColor: color.red,
});
const largeRadiusPerSideColors = tile(450, 495, 180, 85, Object.assign({}, perSideColors, { borderRadius: 40, borderWidth: 20 }));
const circle = tile(660, 495, 85, 85, Object.assign({}, perSideColors, { borderRadius: 60, borderWidth: 14 }));

const container = createView({ flex: 1 });

[
  uniformSquare,
  uniformRounded,
  bottomOnly,
  translucentOverFill,
  translucentRounded,
  perSideAtOneX,
  perSideAtOneAndAHalfX,
  perSideAtTwoX,
  transparentTop,
  transparentTopRounded,
  unsetColor,
  allTransparent,
  oneSideOnly,
  quarterHairline,
  halfHairline,
  threeQuarterHairline,
  onePointHairline,
  roundedHairline,
  clampedRing,
  largeRadiusPerSideWidths,
  largeRadiusPerSideColors,
  circle,
  dashedSquare,
  dashedRounded,
  dottedRounded,
  dashedOverFill,
  dashedPerSide,
].forEach((child) => fabric.appendChild(container, child));

const rootChildren = fabric.createChildSet();

fabric.appendChildToSet(rootChildren, container);
fabric.completeRoot(surfaceId, rootChildren);

console.log('border-matrix: committed surface ' + surfaceId);
