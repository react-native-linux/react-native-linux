// The focus and keyboard proof for issues #37 and #38. Five views in a row: three that can hold focus, one that
// never declared itself accessible, and one that is accessible but disabled — so the traversal order the trace
// prints is the focusable filtering as much as it is the order.
//
// hello_react --focus-tab packages/core/test-bundles/focus.js /tmp/rnl-focus.png 3
//
// Three presses land on epsilon, which is what the golden shows: the ring on the third focusable rather than on
// the third view, because gamma never declared itself accessible and delta is disabled.

const surfaceId = 1;
const fabric = globalThis.nativeFabricUIManager;

if (fabric === undefined) {
  throw new Error('nativeFabricUIManager was not installed');
}

// PointerEventsProcessor and the event target both resolve back to a shadow node through the instance handle, and
// C++ holds it weakly, so every handle is retained here for the same reason React retains them on its fibers.
// The name rides on the handle, which React never looks at: PointerEventsProcessor only ever reads
// instanceHandle.stateNode.node, and the event callback below is handed the same object back.
const createInstanceHandle = (name) => ({ name: name, stateNode: { node: null } });
const handles = [];

const createNode = (tag, name, props) => {
  const handle = createInstanceHandle(name);
  const node = fabric.createNode(tag, 'View', surfaceId, props, handle);

  handle.stateNode.node = node;
  handles.push(handle);

  return node;
};

const container = createNode(2, 'container', { flex: 1 });

const box = (tag, name, left, backgroundColor, accessible, disabled) => {
  const props = {
    position: 'absolute',
    left: left,
    top: 80,
    width: 120,
    height: 80,
    borderRadius: 8,
    backgroundColor: backgroundColor,
    onClick: true,
    onPointerDown: true,
    onPointerUp: true,
  };

  if (accessible) {
    props.accessible = true;
  }

  if (disabled) {
    props.accessibilityState = { disabled: true };
  }

  return createNode(tag, name, props);
};

const alpha = box(10, 'alpha', 40, 0xff3366cc | 0, true, false);
const beta = box(11, 'beta', 200, 0xff33cc66 | 0, true, false);
const gamma = box(12, 'gamma', 360, 0xff555b66 | 0, false, false);
const delta = box(13, 'delta', 520, 0xff995544 | 0, true, true);
const epsilon = box(14, 'epsilon', 640, 0xff9955cc | 0, true, false);

// The single JavaScript entry point for every Fabric event. React's renderer installs its own dispatcher here;
// this one just reports, so the C++ side of the pipeline is what the output describes.
fabric.registerEventHandler((instanceHandle, type, payload) => {
  const name = instanceHandle === null || instanceHandle === undefined ? 'unknown' : instanceHandle.name;
  const hasKey = payload !== null && payload !== undefined && payload.key !== undefined;
  const suffix = hasKey ? ' key=' + payload.key + ' code=' + payload.code : '';

  console.log('focus: ' + type + ' on ' + name + suffix);
});

fabric.appendChild(container, alpha);
fabric.appendChild(container, beta);
fabric.appendChild(container, gamma);
fabric.appendChild(container, delta);
fabric.appendChild(container, epsilon);

const rootChildren = fabric.createChildSet();

fabric.appendChildToSet(rootChildren, container);
fabric.completeRoot(surfaceId, rootChildren);

console.log('focus: committed surface ' + surfaceId);
