// The backgroundImage gradient fixture for the golden-image rig: an angle linear gradient, a corner-keyword one
// with explicit stop percentages, a circle radial, an ellipse radial with an off-centre position, and a
// half-opaque gradient over a solid panel. docs/cpp-toolchain.md, *Gradients*, describes the expected picture.
//
// The layers are written in the shape processBackgroundImage produces rather than as CSS strings: this bundle
// talks to nativeFabricUIManager directly, with no StyleSheet in between, and the native CSS parser behind the
// string form is off by default (ReactNativeFeatureFlags::enableNativeCSSParsing). A real app sends exactly this.

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

const red = 0xffe06c75 | 0;
const blue = 0xff3366cc | 0;
const green = 0xff98c379 | 0;
const amber = 0xffe5c07b | 0;
const white = 0xffffffff | 0;
const slate = 0xff1a1f2b | 0;
const panel = 0xff1e2430 | 0;

const stop = (color, position = null) => ({ color, position });

const angleGradient = createView({
  position: 'absolute',
  left: 40,
  top: 40,
  width: 220,
  height: 160,
  borderRadius: 28,
  experimental_backgroundImage: [
    { type: 'linear-gradient', direction: { type: 'angle', value: 45 }, colorStops: [stop(red), stop(blue)] },
  ],
});

const keywordGradient = createView({
  position: 'absolute',
  left: 300,
  top: 40,
  width: 220,
  height: 160,
  borderRadius: 28,
  experimental_backgroundImage: [
    {
      type: 'linear-gradient',
      direction: { type: 'keyword', value: 'to bottom right' },
      colorStops: [stop(red, '0%'), stop(green, '35%'), stop(blue, '100%')],
    },
  ],
});

const circleGradient = createView({
  position: 'absolute',
  left: 40,
  top: 240,
  width: 220,
  height: 160,
  borderRadius: 24,
  experimental_backgroundImage: [
    {
      type: 'radial-gradient',
      shape: 'circle',
      size: 'farthest-corner',
      position: { top: '50%', left: '50%' },
      colorStops: [stop(white), stop(slate)],
    },
  ],
});

const ellipseGradient = createView({
  position: 'absolute',
  left: 300,
  top: 240,
  width: 220,
  height: 160,
  borderRadius: 24,
  experimental_backgroundImage: [
    {
      type: 'radial-gradient',
      shape: 'ellipse',
      size: 'farthest-corner',
      position: { top: '30%', left: '30%' },
      colorStops: [stop(amber), stop(green, '55%'), stop(panel)],
    },
  ],
});

const halfOpaqueOverSolid = createView(
  { position: 'absolute', left: 560, top: 40, width: 200, height: 360, backgroundColor: amber, borderRadius: 20 },
  [
    createView({
      position: 'absolute',
      left: 20,
      top: 20,
      width: 160,
      height: 320,
      borderRadius: 16,
      opacity: 0.5,
      experimental_backgroundImage: [
        { type: 'linear-gradient', direction: { type: 'angle', value: 180 }, colorStops: [stop(white), stop(slate)] },
      ],
    }),
  ],
);

const container = createView({ flex: 1 }, [
  angleGradient,
  keywordGradient,
  circleGradient,
  ellipseGradient,
  halfOpaqueOverSolid,
]);

const rootChildren = fabric.createChildSet();

fabric.appendChildToSet(rootChildren, container);
fabric.completeRoot(surfaceId, rootChildren);

console.log('gradient: committed surface ' + surfaceId);
