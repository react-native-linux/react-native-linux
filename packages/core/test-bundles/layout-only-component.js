// Issue #150: third-party components whose descriptors are the library's own C++, autolinked unmodified, mount.
// react-native-safe-area-context's RNCSafeAreaProvider and RNCSafeAreaView only lay out, so they mount as Views
// with their children inside. react-native-screens' RNSScreenStackHeaderConfig descriptor reads "RCTImageLoader"
// from the ContextContainer when the node is created, which FabricHost has to answer, and its shadow node's own
// layout moves it to its state's origin, (0, 0), from below the provider where Yoga placed it.
//
// hello_react --fabric packages/core/test-bundles/layout-only-component.js

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

const safeArea = createNode(
  2,
  'RNCSafeAreaProvider',
  { flex: 1, padding: 24 },
  createNode(
    4,
    'RNCSafeAreaView',
    {
      mode: 'padding',
      edges: { top: 'additive', right: 'additive', bottom: 'additive', left: 'additive' },
      flex: 1,
      padding: 16,
      backgroundColor: 0xff1e2430 | 0,
    },
    createNode(6, 'View', { height: 120, backgroundColor: 0xff61afef | 0 }),
  ),
);
const headerConfig = createNode(8, 'RNSScreenStackHeaderConfig', { height: 56 });

const rootChildren = fabric.createChildSet();

fabric.appendChildToSet(rootChildren, safeArea);
fabric.appendChildToSet(rootChildren, headerConfig);
fabric.completeRoot(surfaceId, rootChildren);

console.log('layout-only-component: committed surface ' + surfaceId);
