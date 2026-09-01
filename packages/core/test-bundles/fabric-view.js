// Stands in for React Native's JavaScript renderer. React's Fabric reconciler talks to exactly these
// nativeFabricUIManager methods, so this bundle drives the same C++ commit path a real <View> tree does.

const surfaceId = 1;
const fabric = globalThis.nativeFabricUIManager;

if (fabric === undefined) {
  throw new Error('nativeFabricUIManager was not installed');
}

// C++ holds instance handles weakly, so React retains them on its fibers. Keep them alive for the same reason.
const instanceHandles = [{}, {}];

// Flattened away by Fabric's view flattening: it paints nothing, so it never reaches the mounting layer.
const container = fabric.createNode(2, 'View', surfaceId, { flex: 1, padding: 24 }, instanceHandles[0]);

// Forms a view: a meaningful backgroundColor is one of the conditions that keeps a <View> unflattened.
const box = fabric.createNode(
  4,
  'View',
  surfaceId,
  { width: 120, height: 80, backgroundColor: 0xff3366cc | 0 },
  instanceHandles[1],
);

fabric.appendChild(container, box);

const rootChildren = fabric.createChildSet();

fabric.appendChildToSet(rootChildren, container);
fabric.completeRoot(surfaceId, rootChildren);

console.log('fabric-view: committed surface ' + surfaceId);
