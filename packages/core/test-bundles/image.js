// The <Image> fixture for the golden-image rig: every resizeMode variant, both supported source kinds, and the
// paint props an image shares with a view. docs/cpp-toolchain.md describes the picture this is expected to produce.

const surfaceId = 1;
const fabric = globalThis.nativeFabricUIManager;

if (fabric === undefined) {
  throw new Error('nativeFabricUIManager was not installed');
}

// C++ holds instance handles weakly, so React retains them on its fibers. Keep them alive for the same reason.
const instanceHandles = [];

let nextTag = 2;

const createImage = (props) => {
  const instanceHandle = {};

  instanceHandles.push(instanceHandle);

  const node = fabric.createNode(nextTag, 'Image', surfaceId, props, instanceHandle);

  nextTag += 1;

  return node;
};

const panel = 0xff1e2430 | 0;
const amber = 0xffe5c07b | 0;
const sky = 0xff61afef | 0;

// packages/core/assets/rnl-test-image.png, resolved against RNL_BUNDLED_ASSET_DIR because there is no asset
// packaging yet. A 4x4 data URI stands in for the sources React Native's own tooling inlines.
const assetSource = { uri: 'rnl-test-image.png' };
const dataSource = {
  uri:
    'data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAAAQAAAAECAYAAACp8Z5+AAAAI0lEQVR42mN4kFP6H4RnHK4E' +
    'YwYMgcT17/+D8NMD1WCMIQAAB04uwc/TYjYAAAAASUVORK5CYII=',
};
const remoteSource = { uri: 'https://example.com/there-is-no-networking-stack-yet.png' };

const tile = (left, top, extraProps) => ({
  position: 'absolute',
  left,
  top,
  width: 130,
  height: 120,
  backgroundColor: panel,
  ...extraProps,
});

const resizeModeRow = ['cover', 'contain', 'stretch', 'center', 'repeat'].map((resizeMode, index) =>
  createImage(tile(30 + index * 152, 40, { source: assetSource, resizeMode })),
);

const dataStretch = createImage(tile(30, 200, { source: dataSource, resizeMode: 'stretch' }));
const dataRepeat = createImage(tile(182, 200, { source: dataSource, resizeMode: 'repeat' }));
const rounded = createImage(tile(334, 200, { source: assetSource, resizeMode: 'cover', borderRadius: 28 }));
const bordered = createImage(
  tile(486, 200, {
    source: assetSource,
    resizeMode: 'cover',
    borderRadius: 20,
    borderWidth: 8,
    borderColor: amber,
  }),
);
const translucent = createImage(tile(638, 200, { source: assetSource, resizeMode: 'cover', opacity: 0.4 }));
const tinted = createImage(tile(30, 360, { source: assetSource, resizeMode: 'contain', tintColor: sky }));
const remote = createImage(tile(182, 360, { source: remoteSource, resizeMode: 'cover' }));

const containerHandle = {};

instanceHandles.push(containerHandle);

const container = fabric.createNode(nextTag, 'View', surfaceId, { flex: 1 }, containerHandle);
const children = [
  ...resizeModeRow,
  dataStretch,
  dataRepeat,
  rounded,
  bordered,
  translucent,
  tinted,
  remote,
];

for (const child of children) {
  fabric.appendChild(container, child);
}

const rootChildren = fabric.createChildSet();

fabric.appendChildToSet(rootChildren, container);
fabric.completeRoot(surfaceId, rootChildren);

console.log('image: committed surface ' + surfaceId);
