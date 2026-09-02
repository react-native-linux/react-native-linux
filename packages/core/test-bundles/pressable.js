// The pointer proof for issue #18. It stands in for a <Pressable>: one view that declares the pointer props
// Pressability declares, and one event handler that prints every event Fabric delivers to it.
//
// hello_react --inject-pointer packages/core/test-bundles/pressable.js 200 140

const surfaceId = 1;
const fabric = globalThis.nativeFabricUIManager;

if (fabric === undefined) {
  throw new Error('nativeFabricUIManager was not installed');
}

// PointerEventsProcessor resolves an event target back to a shadow node through instanceHandle.stateNode.node,
// which is the shape React's fiber has. There is no React in this bundle, so the shape is built by hand.
const createInstanceHandle = () => ({ stateNode: { node: null } });

const containerHandle = createInstanceHandle();
const container = fabric.createNode(2, 'View', surfaceId, { flex: 1 }, containerHandle);

containerHandle.stateNode.node = container;

const boxHandle = createInstanceHandle();
const box = fabric.createNode(
  4,
  'View',
  surfaceId,
  {
    position: 'absolute',
    left: 100,
    top: 80,
    width: 200,
    height: 120,
    backgroundColor: 0xff3366cc | 0,
    onPointerEnter: true,
    onPointerLeave: true,
    onPointerMove: true,
    onPointerDown: true,
    onPointerUp: true,
    onClick: true,
  },
  boxHandle,
);

boxHandle.stateNode.node = box;

// The single JavaScript entry point for every Fabric event. React's renderer installs its own dispatcher here;
// this one just reports, so the C++ side of the pipeline is what the output describes.
fabric.registerEventHandler((instanceHandle, type, payload) => {
  const target = instanceHandle === boxHandle ? 'box' : 'unknown';

  console.log('pressable: ' + type + ' on ' + target + ' at ' + payload.clientX + ',' + payload.clientY);
});

fabric.appendChild(container, box);

const rootChildren = fabric.createChildSet();

fabric.appendChildToSet(rootChildren, container);
fabric.completeRoot(surfaceId, rootChildren);

console.log('pressable: committed surface ' + surfaceId);
