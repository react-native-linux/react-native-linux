// The autolinked-component proof for issue #149: `CppLibraryView`, a component spec of the cpp-library fixture
// whose props, shadow node and descriptor @react-native/codegen generated, registered by rnl_autolinking.cpp,
// mounted and laid out like any built-in. Unregistered, the host would print "[component] 'CppLibraryView' has no
// native component registered" instead.
//
// hello_react --fabric packages/core/test-bundles/autolinked-component.js

const surfaceId = 1;
const fabric = globalThis.nativeFabricUIManager;
const instanceHandles = [{}, {}];

const container = fabric.createNode(2, 'View', surfaceId, { flex: 1, padding: 24 }, instanceHandles[0]);
const component = fabric.createNode(
  4,
  'CppLibraryView',
  surfaceId,
  { width: 120, height: 80, backgroundColor: 0xff3366cc | 0, label: 'autolinked' },
  instanceHandles[1],
);

fabric.appendChild(container, component);

const rootChildren = fabric.createChildSet();

fabric.appendChildToSet(rootChildren, container);
fabric.completeRoot(surfaceId, rootChildren);

console.log('autolinked-component: committed surface ' + surfaceId);
