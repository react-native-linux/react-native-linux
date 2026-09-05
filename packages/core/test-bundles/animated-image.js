// The animated <Image> fixture for the golden-image rig: one GIF drawn under every resizeMode a per-frame image
// has to go through, plus one clipped away by an `overflow: hidden` ancestor. docs/cpp-toolchain.md describes the
// picture this is expected to produce.

const surfaceId = 1;
const fabric = globalThis.nativeFabricUIManager;

if (fabric === undefined) {
  throw new Error('nativeFabricUIManager was not installed');
}

// C++ holds instance handles weakly, so React retains them on its fibers. Keep them alive for the same reason.
const instanceHandles = [];

let nextTag = 2;

const create = (componentName, props) => {
  const instanceHandle = {};

  instanceHandles.push(instanceHandle);
  nextTag += 1;

  return fabric.createNode(nextTag - 1, componentName, surfaceId, props, instanceHandle);
};

const panel = 0xff1e2430 | 0;
const sky = 0xff61afef | 0;

// packages/core/assets/rnl-test-animation.gif, resolved against RNL_BUNDLED_ASSET_DIR because there is no asset
// packaging yet. Four frames, a hundred milliseconds each, looping forever; see scripts/make-test-animation.ts.
const source = { uri: 'rnl-test-animation.gif' };

const root = create('View', { flex: 1 });

const box = (left, top, rest) => ({ position: 'absolute', left, top, width: 130, height: 120, ...rest });

const animate = (parent, left, top, rest) => {
  const image = create('Image', { source, ...box(left, top, rest) });

  fabric.appendChild(parent, image);

  return image;
};

for (const [index, resizeMode] of ['cover', 'contain', 'stretch', 'center', 'repeat'].entries()) {
  animate(root, 30 + index * 152, 40, { backgroundColor: panel, resizeMode });
}

animate(root, 30, 200, { backgroundColor: panel, resizeMode: 'cover', borderRadius: 28 });
animate(root, 182, 200, { backgroundColor: panel, resizeMode: 'contain', tintColor: sky });
animate(root, 334, 200, { backgroundColor: panel, resizeMode: 'cover', opacity: 0.4 });

// The clipped-out case: the animation is laid out entirely outside its `overflow: hidden` parent, so it schedules
// no frame and damages nothing while it is off screen. Nothing of it is expected in the picture.
const clipper = create('View', box(486, 200, { backgroundColor: panel, overflow: 'hidden' }));

animate(clipper, 400, 0, { resizeMode: 'cover' });
fabric.appendChild(root, clipper);

const rootChildren = fabric.createChildSet();

fabric.appendChildToSet(rootChildren, root);
fabric.completeRoot(surfaceId, rootChildren);

console.log('animated-image: committed surface ' + surfaceId);
