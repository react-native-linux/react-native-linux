// Issue #150: a third-party component that only lays out mounts as a View. The left half is a tree of <View>s;
// the right half is the same tree with react-native-safe-area-context's RNCSafeAreaProvider and RNCSafeAreaView
// in place of the outer two, given the same props. Autolinked, their descriptors are the library's own, unmodified,
// and no Linux code paints them, so the two halves have to be the same pixels (goldens/golden.spec.ts).
//
// hello_react --fabric packages/core/test-bundles/layout-only-component.js
// hello_react --golden packages/core/test-bundles/layout-only-component.js layout-only-component.png

const surfaceId = 1;
const fabric = globalThis.nativeFabricUIManager;

if (fabric === undefined) {
  throw new Error('nativeFabricUIManager was not installed');
}

// C++ holds instance handles weakly, so React retains them on its fibers. Keep them alive for the same reason.
const instanceHandles = [];

const createNode = (tag, componentName, props, ...children) => {
  const handle = {};
  const node = fabric.createNode(tag, componentName, surfaceId, props, handle);

  instanceHandles.push(handle);

  for (const child of children) {
    fabric.appendChild(node, child);
  }

  return node;
};

const edges = { top: 'additive', right: 'additive', bottom: 'additive', left: 'additive' };

const column = (firstTag, providerName, safeAreaViewName) =>
  createNode(
    firstTag,
    providerName,
    { flex: 1, padding: 24 },
    createNode(
      firstTag + 2,
      safeAreaViewName,
      {
        mode: 'padding',
        edges,
        flex: 1,
        padding: 16,
        backgroundColor: 0xff1e2430 | 0,
        borderWidth: 6,
        borderColor: 0xffe5c07b | 0,
        borderRadius: 28,
        overflow: 'hidden',
        opacity: 0.9,
      },
      createNode(firstTag + 4, 'View', { height: 120, backgroundColor: 0xff61afef | 0 }),
      createNode(firstTag + 6, 'View', {
        position: 'absolute',
        right: -40,
        bottom: -40,
        width: 160,
        height: 160,
        borderRadius: 80,
        backgroundColor: 0xff98c379 | 0,
      }),
    ),
  );

const row = createNode(
  2,
  'View',
  { flex: 1, flexDirection: 'row' },
  column(4, 'View', 'View'),
  column(20, 'RNCSafeAreaProvider', 'RNCSafeAreaView'),
);

const rootChildren = fabric.createChildSet();

fabric.appendChildToSet(rootChildren, row);
fabric.completeRoot(surfaceId, rootChildren);

console.log('layout-only-component: committed surface ' + surfaceId);
