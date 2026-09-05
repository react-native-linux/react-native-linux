// The fixture for issue #244: one 200x150 <ScrollView> over a single 200x600 row that stays under the pointer
// however far the wheel moves the content, so a press and a release at the same point land on the same node and
// the only reason the release is not a click is the scroll between them.
//
// Every line carries the running click count, which is what makes the trace assertion a statement about a click
// that did not happen: the press after the cancelled one still reads clicks=0.

const surfaceId = 1;
const fabric = globalThis.nativeFabricUIManager;

if (fabric === undefined) {
  throw new Error('nativeFabricUIManager was not installed');
}

const createInstanceHandle = () => ({ stateNode: { node: null } });
const namesByHandle = new Map();

const createNode = (tag, componentName, name, props) => {
  const handle = createInstanceHandle();
  const node = fabric.createNode(tag, componentName, surfaceId, props, handle);

  handle.stateNode.node = node;
  namesByHandle.set(handle, name);

  return node;
};

const container = createNode(2, 'View', 'container', { flex: 1 });

const list = createNode(3, 'ScrollView', 'list', {
  position: 'absolute',
  left: 60,
  top: 60,
  width: 200,
  height: 150,
  backgroundColor: 0xff1e2430 | 0,
  onScroll: true,
  onScrollBeginDrag: true,
  onScrollEndDrag: true,
});

const row = createNode(4, 'View', 'row', {
  position: 'absolute',
  left: 0,
  top: 0,
  width: 200,
  height: 600,
  backgroundColor: 0xff3366cc | 0,
  onPointerDown: true,
  onPointerUp: true,
  onClick: true,
});

let clickCount = 0;

fabric.registerEventHandler((instanceHandle, type) => {
  if (type === 'topClick') {
    clickCount += 1;
  }

  const name = namesByHandle.get(instanceHandle);

  console.log('press-scroll: ' + type + ' on ' + (name === undefined ? 'unknown' : name) + ' clicks=' + clickCount);
});

fabric.appendChild(list, row);
fabric.appendChild(container, list);

const rootChildren = fabric.createChildSet();

fabric.appendChildToSet(rootChildren, container);
fabric.completeRoot(surfaceId, rootChildren);

console.log('press-scroll: committed surface ' + surfaceId);
