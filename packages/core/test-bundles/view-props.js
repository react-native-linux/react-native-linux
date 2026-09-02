// The <View> visual-prop fixture for the golden-image rig. Every prop group the renderer claims to support gets
// one visually unambiguous element; docs/cpp-toolchain.md describes the picture this is expected to produce.

const surfaceId = 1;
const fabric = globalThis.nativeFabricUIManager;

if (fabric === undefined) {
  throw new Error('nativeFabricUIManager was not installed');
}

// C++ holds instance handles weakly, so React retains them on its fibers. Keep them alive for the same reason.
const instanceHandles = [];

let nextTag = 2;

const createView = (props, children = []) => {
  const instanceHandle = {};

  instanceHandles.push(instanceHandle);

  const node = fabric.createNode(nextTag, 'View', surfaceId, props, instanceHandle);

  nextTag += 1;

  for (const child of children) {
    fabric.appendChild(node, child);
  }

  return node;
};

const blue = 0xff3366cc | 0;
const panel = 0xff1e2430 | 0;
const amber = 0xffe5c07b | 0;
const red = 0xffe06c75 | 0;
const green = 0xff98c379 | 0;
const sky = 0xff61afef | 0;
const purple = 0xffc678dd | 0;
const cyan = 0xff56b6c2 | 0;
const white = 0xffffffff | 0;
const slate = 0xff1a1f2b | 0;

const perCornerRadii = createView({
  position: 'absolute',
  left: 40,
  top: 40,
  width: 160,
  height: 120,
  backgroundColor: blue,
  borderTopLeftRadius: 40,
  borderBottomRightRadius: 40,
});

const uniformBorder = createView({
  position: 'absolute',
  left: 230,
  top: 40,
  width: 160,
  height: 120,
  backgroundColor: panel,
  borderRadius: 24,
  borderWidth: 10,
  borderColor: amber,
});

const perSideBorder = createView({
  position: 'absolute',
  left: 420,
  top: 40,
  width: 160,
  height: 120,
  backgroundColor: panel,
  borderLeftWidth: 6,
  borderTopWidth: 12,
  borderRightWidth: 18,
  borderBottomWidth: 24,
  borderLeftColor: red,
  borderTopColor: green,
  borderRightColor: sky,
  borderBottomColor: purple,
});

const clampedRadii = createView({
  position: 'absolute',
  left: 610,
  top: 70,
  width: 150,
  height: 60,
  backgroundColor: cyan,
  borderRadius: 200,
});

const nestedOpacity = createView(
  { position: 'absolute', left: 40, top: 200, width: 200, height: 140, backgroundColor: red, opacity: 0.5 },
  [createView({ position: 'absolute', left: 30, top: 30, width: 140, height: 80, backgroundColor: white, opacity: 0.5 })],
);

const clippedOverflow = createView(
  {
    position: 'absolute',
    left: 280,
    top: 200,
    width: 200,
    height: 140,
    backgroundColor: panel,
    borderRadius: 28,
    overflow: 'hidden',
  },
  [createView({ position: 'absolute', left: 60, top: 60, width: 300, height: 220, backgroundColor: green })],
);

const transformed = createView({
  position: 'absolute',
  left: 520,
  top: 200,
  width: 160,
  height: 110,
  backgroundColor: sky,
  borderRadius: 12,
  transform: [{ rotate: '-15deg' }, { scale: 1.15 }],
});

const stacked = createView(
  { position: 'absolute', left: 40, top: 380, width: 340, height: 200, backgroundColor: slate },
  [
    createView({ position: 'absolute', left: 10, top: 10, width: 150, height: 150, backgroundColor: red, zIndex: 3 }),
    createView({ position: 'absolute', left: 60, top: 25, width: 150, height: 150, backgroundColor: green, zIndex: 1 }),
    createView({ position: 'absolute', left: 110, top: 40, width: 150, height: 150, backgroundColor: sky, zIndex: 2 }),
  ],
);

const container = createView({ flex: 1 }, [
  perCornerRadii,
  uniformBorder,
  perSideBorder,
  clampedRadii,
  nestedOpacity,
  clippedOverflow,
  transformed,
  stacked,
]);

const rootChildren = fabric.createChildSet();

fabric.appendChildToSet(rootChildren, container);
fabric.completeRoot(surfaceId, rootChildren);

console.log('view-props: committed surface ' + surfaceId);
