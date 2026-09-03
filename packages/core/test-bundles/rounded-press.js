// The rounded-corner pointer proof for issue #99, driven end to end: the box is a 200x120 card with a 60-point
// radius, so its top-left corner arc is centred at (160, 140). A click at (105, 85) is five points inside the
// frame on both axes and 77.8 from that centre — outside the arc, and therefore outside the card, because the
// card was never painted there. A click at (200, 140) is on the card itself.
//
// hello_react --inject-pointer packages/core/test-bundles/rounded-press.js 105 85

const surfaceId = 1;
const fabric = globalThis.nativeFabricUIManager;

if (fabric === undefined) {
  throw new Error('nativeFabricUIManager was not installed');
}

// PointerEventsProcessor resolves a target back to a shadow node through instanceHandle.stateNode.node, which is
// the shape React's fiber has. There is no React here, so the shape is built by hand.
const names = new Map();

const createView = (name, tag, props) => {
  const instanceHandle = { stateNode: { node: null } };
  const node = fabric.createNode(tag, 'View', surfaceId, props, instanceHandle);

  instanceHandle.stateNode.node = node;
  names.set(instanceHandle, name);

  return node;
};

const pointerProps = {
  onPointerEnter: true,
  onPointerLeave: true,
  onPointerMove: true,
  onPointerDown: true,
  onPointerUp: true,
  onClick: true,
};

// A background colour, because Fabric flattens a <View> that paints nothing away before the mounting layer ever
// sees it, and this one has to be a pointer target.
const container = createView('container', 2, { ...pointerProps, flex: 1, backgroundColor: 0xff1e2430 | 0 });
const card = createView('card', 4, {
  ...pointerProps,
  position: 'absolute',
  left: 100,
  top: 80,
  width: 200,
  height: 120,
  borderRadius: 60,
  backgroundColor: 0xff3366cc | 0,
});

fabric.registerEventHandler((instanceHandle, type, payload) => {
  const name = names.get(instanceHandle) ?? 'unknown';

  console.log('rounded-press: ' + type + ' on ' + name + ' at ' + payload.clientX + ',' + payload.clientY);
});

fabric.appendChild(container, card);

const rootChildren = fabric.createChildSet();

fabric.appendChildToSet(rootChildren, container);
fabric.completeRoot(surfaceId, rootChildren);

console.log('rounded-press: committed surface ' + surfaceId);
