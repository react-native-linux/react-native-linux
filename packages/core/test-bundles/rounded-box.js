// The one-rounded-box fixture for the golden-image rig, issue #99: at a large radius, the background fill, the
// gradient layer, the border ring, the content clip and the `overflow: hidden` child clip all have to be cut by
// the same arc. Every card below is a case where two of those five meet on a corner; if any consumer computed a
// rounded rect of its own, the arcs would separate and this image would change.
//
// docs/cpp-toolchain.md, *View props fidelity*, describes the expected picture.

const surfaceId = 1;
const fabric = globalThis.nativeFabricUIManager;

if (fabric === undefined) {
  throw new Error('nativeFabricUIManager was not installed');
}

// C++ holds instance handles weakly, so React retains them on its fibers. Keep them alive for the same reason.
const instanceHandles = [];

const createView = (props, ...children) => {
  const handle = {};
  const node = fabric.createNode(instanceHandles.length + 2, 'View', surfaceId, props, handle);

  instanceHandles.push(handle);

  for (const child of children) {
    fabric.appendChild(node, child);
  }

  return node;
};

const panel = 0xff1e2430 | 0;
const amber = 0xffe5c07b | 0;
const sky = 0xff61afef | 0;
const green = 0xff98c379 | 0;
const purple = 0xffc678dd | 0;
const cyan = 0xff56b6c2 | 0;
const red = 0xffe06c75 | 0;
const slate = 0xff1a1f2b | 0;

const stop = (color, position = null) => ({ color, position });

// Fill and ring under a child that overflows two of the four corners: the child stops on the same arc the ring
// draws, and it draws over the ring where it reaches it, which is what `clipsToBounds` does on iOS.
const clippedChild = createView(
  {
    position: 'absolute',
    left: 60,
    top: 60,
    width: 320,
    height: 240,
    backgroundColor: panel,
    borderRadius: 96,
    borderWidth: 16,
    borderColor: amber,
    overflow: 'hidden',
  },
  createView({ position: 'absolute', left: 40, top: 120, width: 400, height: 300, backgroundColor: sky }),
);

// A gradient layer is the content of this one, and it has to stop on the same arc the solid fill would have.
const clippedGradient = createView({
  position: 'absolute',
  left: 440,
  top: 60,
  width: 320,
  height: 240,
  borderRadius: 96,
  borderWidth: 16,
  borderColor: sky,
  experimental_backgroundImage: [
    {
      type: 'linear-gradient',
      direction: { type: 'keyword', value: 'to bottom right' },
      colorStops: [stop(purple), stop(green, '55%'), stop(slate)],
    },
  ],
});

// The overlap clamp at work: 200 on a 320x180 box becomes 90, so this is a pill, and the ring's inner edge is the
// same pill inset by 24 on every axis.
const clampedRing = createView({
  position: 'absolute',
  left: 60,
  top: 360,
  width: 320,
  height: 180,
  backgroundColor: cyan,
  borderRadius: 200,
  borderWidth: 24,
  borderColor: purple,
});

// A child exactly the size of its clipping parent: every pixel of the child's own square corners has to be cut,
// so the picture is the parent's pill in the child's colour with the parent's ring around it.
const clippedToThePill = createView(
  {
    position: 'absolute',
    left: 440,
    top: 360,
    width: 320,
    height: 180,
    backgroundColor: panel,
    borderRadius: 200,
    borderWidth: 24,
    borderColor: amber,
    overflow: 'hidden',
  },
  createView({ position: 'absolute', left: 24, top: 24, width: 320, height: 180, backgroundColor: red }),
);

const container = createView({ flex: 1 }, clippedChild, clippedGradient, clampedRing, clippedToThePill);
const rootChildren = fabric.createChildSet();

fabric.appendChildToSet(rootChildren, container);
fabric.completeRoot(surfaceId, rootChildren);

console.log('rounded-box: committed surface ' + surfaceId);
