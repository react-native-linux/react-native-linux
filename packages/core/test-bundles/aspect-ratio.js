// The aspectRatio constraint fixture for the golden-image rig, issue #117: the combination users actually write.
// One image sized by width + aspectRatio, one clamped by maxWidth (which re-derives the height through the ratio),
// and one panel showing the clamped box beside the unclamped one for contrast.
//
// hello_react --golden packages/core/test-bundles/aspect-ratio.js /tmp/rnl-aspect-ratio.png

const surfaceId = 1;
const fabric = globalThis.nativeFabricUIManager;

if (fabric === undefined) {
  throw new Error('nativeFabricUIManager was not installed');
}

const nodes = new Map();

const createNode = (tag, componentName, props) => {
  const node = fabric.createNode(tag, componentName, surfaceId, props, {});

  nodes.set(tag, node);

  return node;
};

const source = { uri: 'rnl-test-image.png' };
const panel = 0xff1e2430 | 0;

// 240 wide at a 2:1 ratio, unclamped: 240x120.
const natural = createNode(10, 'Image', {
  position: 'absolute',
  left: 40,
  top: 80,
  width: 240,
  aspectRatio: 2,
  source,
  backgroundColor: panel,
});

// The same image under maxWidth 150: the width clamps to 150 and the height re-derives to 75.
const clamped = createNode(11, 'Image', {
  position: 'absolute',
  left: 40,
  top: 240,
  width: 240,
  maxWidth: 150,
  aspectRatio: 2,
  source,
  backgroundColor: panel,
});

const container = createNode(12, 'View', { flex: 1 });

fabric.appendChild(container, natural);
fabric.appendChild(container, clamped);

const rootChildren = fabric.createChildSet();

fabric.appendChildToSet(rootChildren, container);
fabric.completeRoot(surfaceId, rootChildren);

console.log('aspect-ratio: committed surface ' + surfaceId);
