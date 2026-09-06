// Issue #301: the window between a settled decode queue and the commit an `onLoad` handler makes off it. The
// decode publishes on the pipeline's worker thread, `onLoad` fires from that same thread through the coordinator
// every `ImageRequest` carries, and the handler below turns straight around and commits a tint — the shape of the
// race the golden settle has to close, because a settle that only waits on the decode queue can return before
// that commit lands.
//
// hello_react --golden packages/core/test-bundles/image-onload.js /tmp/rnl-image-onload.png

const surfaceId = 1;
const fabric = globalThis.nativeFabricUIManager;

if (fabric === undefined) {
  throw new Error('nativeFabricUIManager was not installed');
}

// C++ holds instance handles weakly, so React retains them on its fibers. Keep them alive for the same reason.
const handles = [{}, {}];

const sky = 0xff61afef | 0;

// packages/core/assets/rnl-test-image.png, resolved against RNL_BUNDLED_ASSET_DIR because there is no asset
// packaging yet.
const source = { uri: 'rnl-test-image.png' };

const image = fabric.createNode(
  2,
  'Image',
  surfaceId,
  { position: 'absolute', left: 60, top: 60, width: 200, height: 150, resizeMode: 'contain', source, onLoad: true },
  handles[0],
);
const container = fabric.createNode(3, 'View', surfaceId, { flex: 1 }, handles[1]);

fabric.appendChild(container, image);

const firstChildren = fabric.createChildSet();

fabric.appendChildToSet(firstChildren, container);
fabric.completeRoot(surfaceId, firstChildren);

console.log('image-onload: committed surface ' + surfaceId);

let hasTinted = false;

fabric.registerEventHandler((instanceHandle, type) => {
  console.log('image-onload: event ' + type);

  if (type !== 'topLoad' || hasTinted) {
    return;
  }

  hasTinted = true;

  const tintedImage = fabric.cloneNodeWithNewProps(image, {
    position: 'absolute',
    left: 60,
    top: 60,
    width: 200,
    height: 150,
    resizeMode: 'contain',
    source,
    onLoad: true,
    tintColor: sky,
  });
  const nextContainer = fabric.cloneNodeWithNewChildren(container);

  fabric.appendChild(nextContainer, tintedImage);

  const secondChildren = fabric.createChildSet();

  fabric.appendChildToSet(secondChildren, nextContainer);
  fabric.completeRoot(surfaceId, secondChildren);

  console.log('image-onload: committed the tint onLoad produced');
});
