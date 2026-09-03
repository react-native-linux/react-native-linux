// The flexWrap conformance fixture for issue #118: one wrapping grid of nine tiles laid out at two container
// widths, so a line-break regression is a visible difference instead of a number in a table.
//
// hello_react --golden packages/core/test-bundles/wrapping.js /tmp/rnl-wrapping.png
//
// The two grids hold identical 70x50 tiles with a 10-point gap. The 300-point-wide container fits three tiles
// per line (three lines); the 180-point one fits two (five lines, last one cut by the container height). Both
// containers clip their overflow, so the image shows exactly where each break lands.

const surfaceId = 1;
const fabric = globalThis.nativeFabricUIManager;

if (fabric === undefined) {
  throw new Error('nativeFabricUIManager was not installed');
}

// C++ holds instance handles weakly, so the bundle retains every one it created, keyed by tag for the asserts
// nothing in a static fixture needs today.
const nodes = new Map();

const createView = (tag, props, children = []) => {
  const node = fabric.createNode(tag, 'View', surfaceId, props, {});

  nodes.set(tag, node);
  children.forEach((child) => fabric.appendChild(node, child));

  return node;
};

const tileColors = [
  0xffe06c75 | 0,
  0xff98c379 | 0,
  0xff61afef | 0,
  0xffe5c07b | 0,
  0xffc678dd | 0,
  0xff56b6c2 | 0,
  0xffe06c75 | 0,
  0xff98c379 | 0,
  0xff61afef | 0,
];

let nextTileTag = 20;

const tile = (color) => createView(nextTileTag++, { width: 70, height: 50, backgroundColor: color });

const wrappingGrid = (tag, left, top, width, height) =>
  createView(
    tag,
    {
      position: 'absolute',
      left,
      top,
      width,
      height,
      backgroundColor: 0xff1e2430 | 0,
      flexDirection: 'row',
      flexWrap: 'wrap',
      gap: 10,
      overflow: 'hidden',
    },
    tileColors.map(tile),
  );

// Nine 70-point tiles with 10-point gaps: three lines of three at 300 points wide, two per line at 180.
const container = createView(12, { flex: 1 }, [
  wrappingGrid(10, 40, 60, 300, 190),
  wrappingGrid(11, 400, 60, 180, 190),
]);

const rootChildren = fabric.createChildSet();

fabric.appendChildToSet(rootChildren, container);
fabric.completeRoot(surfaceId, rootChildren);

console.log('wrapping: committed surface ' + surfaceId);
