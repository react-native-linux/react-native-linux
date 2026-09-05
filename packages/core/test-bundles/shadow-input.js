// The keystroke fixture behind issue #102's e2e layer, and core#47920: a `boxShadow` on a `<TextInput>` that
// alternated visible and hidden on every keystroke, because each edit repainted the field's frame and the shadow
// lives outside it. One field, one shadow, and five characters typed into it — five damage cycles in a row, each
// of which has to leave the shadow on screen.
//
// The trace is the field's own change events; the picture is the screenshot the scenario blesses, and a shadow
// that flickered would be missing from whichever frame the capture landed on. The palette is light and the shadow
// is black for that reason: on a dark surface a missing shadow and a drawn one are the same screenshot.

const surfaceId = 1;
const fabric = globalThis.nativeFabricUIManager;

if (fabric === undefined) {
  throw new Error('nativeFabricUIManager was not installed');
}

const rootHandle = { name: 'root', stateNode: { node: null } };
const root = fabric.createNode(2, 'View', surfaceId, { flex: 1, backgroundColor: 0xfff2f4f8 | 0 }, rootHandle);

rootHandle.stateNode.node = root;

const fieldHandle = { name: 'shadowed', stateNode: { node: null } };
const field = fabric.createNode(
  3,
  'TextInput',
  surfaceId,
  {
    position: 'absolute',
    left: 120,
    top: 120,
    width: 360,
    height: 56,
    paddingLeft: 14,
    paddingRight: 14,
    paddingTop: 12,
    paddingBottom: 12,
    borderWidth: 2,
    borderRadius: 12,
    borderColor: 0xffb9c1cf | 0,
    backgroundColor: 0xffffffff | 0,
    color: 0xff1e2430 | 0,
    fontSize: 20,
    accessible: true,
    cursorColor: 0xff2f6feb | 0,
    selectionColor: 0x592f6feb | 0,
    placeholder: 'Type here',
    placeholderTextColor: 0xff8b93a1 | 0,
    boxShadow: [{ offsetX: 0, offsetY: 12, blurRadius: 22, spreadDistance: 0, color: 0xb3000000 | 0, inset: false }],
  },
  fieldHandle,
);

fieldHandle.stateNode.node = field;

fabric.registerEventHandler((instanceHandle, type, payload) => {
  const target = instanceHandle === fieldHandle ? 'shadowed' : 'unknown';
  const text = payload !== null && payload !== undefined && payload.text !== undefined ? ' text="' + payload.text + '"' : '';

  console.log('shadow-input: ' + type + ' on ' + target + text);
});

fabric.appendChild(root, field);

const rootChildren = fabric.createChildSet();

fabric.appendChildToSet(rootChildren, root);
fabric.completeRoot(surfaceId, rootChildren);

console.log('shadow-input: committed surface ' + surfaceId);
