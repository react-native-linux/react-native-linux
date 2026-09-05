// The <Image> blurRadius fixture for the golden-image rig, issue #286: every resizeMode variant blurred at 8
// points, and one animated tile blurred the same way. docs/cpp-toolchain.md describes the picture this is
// expected to produce.

const surfaceId = 1;
const fabric = globalThis.nativeFabricUIManager;

if (fabric === undefined) {
  throw new Error('nativeFabricUIManager was not installed');
}

// C++ holds instance handles weakly, so React retains them on its fibers. Keep them alive for the same reason.
const instanceHandles = [];
let nextImageTag = 2;

const mountBlurredImage = (source, resizeMode, left, top, blurRadius) => {
  const handle = {};

  instanceHandles.push(handle);

  const props = {
    position: 'absolute',
    left,
    top,
    width: 130,
    height: 120,
    backgroundColor: 0xff1e2430 | 0,
    source,
    resizeMode,
    blurRadius,
  };
  const node = fabric.createNode(nextImageTag, 'Image', surfaceId, props, handle);

  nextImageTag += 1;

  return node;
};

// The same fixtures image.js and animated-image.js use, resolved against RNL_BUNDLED_ASSET_DIR.
const stillSource = { uri: 'rnl-test-image.png' };
const animatedSource = { uri: 'rnl-test-animation.gif' };

const children = ['cover', 'contain', 'stretch', 'center', 'repeat'].map((resizeMode, index) =>
  mountBlurredImage(stillSource, resizeMode, 30 + index * 152, 40, 8),
);

// blurRadius: 0 is the plain image, painted byte-identical to today: this tile proves the two draw side by side.
children.push(mountBlurredImage(stillSource, 'cover', 30, 200, 0));

// The tile #286 owes #257: the same GIF, blurred, at the same frame the --animated-image flag settles on.
children.push(mountBlurredImage(animatedSource, 'cover', 182, 200, 8));

const containerHandle = {};

instanceHandles.push(containerHandle);

const container = fabric.createNode(nextImageTag, 'View', surfaceId, { flex: 1 }, containerHandle);

children.forEach((child) => fabric.appendChild(container, child));

const rootChildren = fabric.createChildSet();

fabric.appendChildToSet(rootChildren, container);
fabric.completeRoot(surfaceId, rootChildren);

console.log('image-blur: committed surface ' + surfaceId);
