// The mouse-button payload fixture for the e2e rig, issue #66: one box that reports every pointer event with the
// W3C `button` and `buttons` numbers it carried, so the compositor-to-JavaScript payload is what the output
// describes.
//
// scripts/e2e.ts drives this with packages/core/e2e/mouse-button.json: a right press/release over the box must
// reach JavaScript as button=2 with no click, and a left click after it must be button=0 with the click.

const surfaceId = 1;
const fabric = globalThis.nativeFabricUIManager;

if (fabric === undefined) {
  throw new Error('nativeFabricUIManager was not installed');
}

// C++ holds instance handles weakly, so the bundle retains one per node, pre-shaped the way the pointer event
// target resolution reads them back.
const boxHandle = { stateNode: { node: null } };
const containerHandle = { stateNode: { node: null } };

const box = fabric.createNode(
  2,
  'View',
  surfaceId,
  {
    position: 'absolute',
    left: 100,
    top: 80,
    width: 200,
    height: 120,
    backgroundColor: 0xff3366cc | 0,
    onPointerDown: true,
    onPointerUp: true,
    onClick: true,
  },
  boxHandle,
);

boxHandle.stateNode.node = box;

// The single JavaScript entry point for every Fabric event. React's renderer installs its own dispatcher here;
// this one just reports, so the C++ side of the pipeline is what the output describes. One line per event, built
// from the payload's own W3C numbers - which is the whole point of the fixture.
fabric.registerEventHandler((instanceHandle, type, payload) => {
  const parts = ['mouse-button:', type, 'on', instanceHandle === boxHandle ? 'box' : 'unknown'];

  parts.push(`button=${payload.button}`);
  parts.push(`buttons=${payload.buttons}`);
  parts.push(`at ${payload.clientX},${payload.clientY}`);

  console.log(parts.join(' '));
});

const container = fabric.createNode(3, 'View', surfaceId, { flex: 1 }, containerHandle);

containerHandle.stateNode.node = container;

fabric.appendChild(container, box);

const rootChildren = fabric.createChildSet();

fabric.appendChildToSet(rootChildren, container);
fabric.completeRoot(surfaceId, rootChildren);

console.log('mouse-button: committed surface ' + surfaceId);
