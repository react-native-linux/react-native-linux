// The <Image> fixture for issue #258: capInsets nine-slice at two sizes, and defaultSource drawn in place of a
// real source that never decodes. docs/cpp-toolchain.md describes the picture this is expected to produce.

const surfaceId = 1;
const fabric = globalThis.nativeFabricUIManager;

if (fabric === undefined) {
  throw new Error('nativeFabricUIManager was not installed');
}

// C++ holds instance handles weakly, so React retains them on its fibers. Keep them alive for the same reason.
const instanceHandles = [];
let nextPropsImageTag = 2;

const mountPropsImage = (extraProps, left, top, width, height) => {
  const handle = {};

  instanceHandles.push(handle);

  const props = { position: 'absolute', left, top, width, height, backgroundColor: 0xff1e2430 | 0, ...extraProps };
  const node = fabric.createNode(nextPropsImageTag, 'Image', surfaceId, props, handle);

  nextPropsImageTag += 1;

  return node;
};

// packages/core/assets/rnl-test-image.png: a 64x48 PNG with a 2-point light border around four colour quadrants
// and a dark centre square, resolved against RNL_BUNDLED_ASSET_DIR because there is no asset packaging yet.
const quadrantSource = { uri: 'rnl-test-image.png' };

// A path nothing decodes: `resolveImageSource` resolves it to a file that does not exist, so the pipeline queues
// the decode, the codec fails to open it, and the completion runs with null forever. That is what keeps
// `defaultSource` on screen for the whole render instead of it being a timing race against a real decode.
const missingSource = { uri: 'rnl-test-image-that-does-not-exist.png' };

// Nine-slice at two sizes: 16 points cut from every edge of the 64x48 asset leaves a 32x16 centre — one that
// straddles all four quadrants — so the corners a nine-slice keeps at their native size and the centre it alone
// stretches are both readable at a glance, next to a plain `cover` fit of the same asset that scales everything
// uniformly instead.
const buttonCapInsets = { top: 16, left: 16, right: 16, bottom: 16 };

const children = [
  mountPropsImage({ source: quadrantSource, resizeMode: 'cover' }, 30, 40, 100, 60),
  mountPropsImage({ source: quadrantSource, capInsets: buttonCapInsets }, 30, 120, 100, 60),
  mountPropsImage({ source: quadrantSource, resizeMode: 'cover' }, 160, 40, 220, 140),
  mountPropsImage({ source: quadrantSource, capInsets: buttonCapInsets }, 160, 200, 220, 140),
];

// defaultSource stays on screen for the whole render because `missingSource` never decodes; the settle
// BundleRunner performs before every scene read (docs/cpp-toolchain.md, *Image*) waits for that failed decode to
// publish its null, and null never replaces the placeholder `damageImageSource` already attached.
children.push(
  mountPropsImage(
    { source: missingSource, defaultSource: quadrantSource, resizeMode: 'contain' },
    420,
    40,
    130,
    120,
  ),
);

// loadingIndicatorSource is the same slot as defaultSource on this platform: with no defaultSource configured, it
// is what draws in its place.
children.push(
  mountPropsImage(
    { source: missingSource, loadingIndicatorSource: quadrantSource, resizeMode: 'contain' },
    420,
    200,
    130,
    120,
  ),
);

const containerHandle = {};

instanceHandles.push(containerHandle);

const container = fabric.createNode(nextPropsImageTag, 'View', surfaceId, { flex: 1 }, containerHandle);
const rootChildren = fabric.createChildSet();

// Six tiles: the four capInsets tiles and the two placeholder ones, mounted under the one full-screen container.
for (const child of children) {
  fabric.appendChild(container, child);
}

fabric.appendChildToSet(rootChildren, container);
fabric.completeRoot(surfaceId, rootChildren);
console.log('image-props: committed surface ' + surfaceId);
