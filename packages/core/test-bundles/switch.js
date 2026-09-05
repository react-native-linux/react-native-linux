// The <Switch> fixture for issue #261, one tile per state the control can be in:
//
//   1  off, platform colours                the track at its off colour, the thumb at the left end
//   2  on, platform colours                 the track at its on colour, the thumb at the right end
//   3  off, disabled                        the same drawing at half strength
//   4  on, disabled                         likewise, so the dimming is visible against tile 2
//   5  off, trackColor and thumbColor       tintColor / onTintColor / thumbTintColor, as Switch.js sends them
//   6  on, trackColor and thumbColor        the same three props with the thumb at the other end
//   7  the controlled toggle                off until a press, then whatever this bundle commits back
//
// Tile 7 is the whole of the control's contract: the press changes no pixel by itself, it fires onChange, and
// what moves the thumb is the value this handler commits in reply. `--golden` renders it untouched and
// `--clicked-frame` renders it a named number of frames after a click, which is where it is caught mid-travel.
//
// No tile gives the control a width or a height: the size is SwitchShadowNode::measureContent, which is what
// react-native-macos#1699 is about. docs/cpp-toolchain.md, *Switch (#261)*, describes the expected picture.

const surfaceId = 1;
const fabric = globalThis.nativeFabricUIManager;

if (fabric === undefined) {
  throw new Error('nativeFabricUIManager was not installed');
}

const handles = [];

// PointerEventsProcessor resolves an event target back to a shadow node through instanceHandle.stateNode.node,
// which is the shape React's fiber has. There is no React in this bundle, so the shape is built by hand.
const createInstanceHandle = () => ({ stateNode: { node: null } });

const node = (componentName, props) => {
  const handle = createInstanceHandle();
  const created = fabric.createNode(2 + handles.length, componentName, surfaceId, props, handle);

  handle.stateNode.node = created;
  handles.push(handle);

  return created;
};

const colour = { track: 0xff8a4fff | 0, thumb: 0xffffd166 | 0 };

const switchAt = (left, top, props) =>
  node('Switch', Object.assign({ position: 'absolute', left, top }, props));

const container = node('View', { flex: 1 });

const tiles = [
  switchAt(60, 60, { value: false }),
  switchAt(200, 60, { value: true }),
  switchAt(60, 140, { value: false, disabled: true }),
  switchAt(200, 140, { value: true, disabled: true }),
  switchAt(60, 220, { value: false, tintColor: colour.track, thumbTintColor: colour.thumb }),
  switchAt(200, 220, { value: true, onTintColor: colour.track, thumbTintColor: colour.thumb }),
];

const toggleHandle = createInstanceHandle();
let toggle = fabric.createNode(
  2 + handles.length,
  'Switch',
  surfaceId,
  { position: 'absolute', left: 60, top: 300, value: false, onChange: true },
  toggleHandle,
);

toggleHandle.stateNode.node = toggle;
handles.push(toggleHandle);

const commit = (root) => {
  const rootChildren = fabric.createChildSet();

  fabric.appendChildToSet(rootChildren, root);
  fabric.completeRoot(surfaceId, rootChildren);
};

const commitTree = () => {
  const nextContainer = fabric.cloneNodeWithNewChildren(container);

  tiles.forEach((tile) => fabric.appendChild(nextContainer, tile));
  fabric.appendChild(nextContainer, toggle);
  commit(nextContainer);
};

fabric.registerEventHandler((instanceHandle, type, payload) => {
  if (instanceHandle !== toggleHandle || type !== 'topChange') {
    return;
  }

  console.log('switch: ' + type + ' value=' + payload.value);

  toggle = fabric.cloneNodeWithNewProps(toggle, { value: payload.value });
  toggleHandle.stateNode.node = toggle;
  commitTree();

  console.log('switch: onValueChange ' + payload.value);
});

tiles.forEach((tile) => fabric.appendChild(container, tile));
fabric.appendChild(container, toggle);
commit(container);

console.log('switch: committed surface ' + surfaceId);
