// The composition-across-fields proof for issue #340: two fields, so a composition that starts in one can be
// abandoned by focusing the other, and the field it lands in is not the field it started in.
//
// The run is driven by `rnl_window --inject-key-sequence`, whose `{Preedit:...}` and `{Commit:...}` tokens are
// the events a zwp_text_input_v3 input method would have sent and whose `{SessionLeave}`/`{SessionEnter}` pair
// replays a keyboard leave and enter. cage runs no input method and cannot take its only window's keyboard
// focus, which is why the events are injected at the frame batch they would otherwise arrive in.
//
// The trace lines the scenario reads are the bundle's own change events — the committed text names the field it
// landed in — beside the platform's `[rnl-ime] session enabled`/`session disabled` lines, which are the session
// lifecycle the field-focus traces cannot distinguish from a re-enable after a leave.

const surfaceId = 1;
const fabric = globalThis.nativeFabricUIManager;

if (fabric === undefined) {
  throw new Error('nativeFabricUIManager was not installed');
}

const handles = [];

const style = (left) => ({
  position: 'absolute',
  left: left,
  top: 40,
  width: 320,
  height: 48,
  borderWidth: 2,
  backgroundColor: 0xff1e2430 | 0,
  color: 0xfff2f4f8 | 0,
  fontSize: 18,
  accessible: true,
});

const containerHandle = { name: 'container', stateNode: { node: null } };
const container = fabric.createNode(2, 'View', surfaceId, { flex: 1 }, containerHandle);
containerHandle.stateNode.node = container;

for (const [index, name] of ['first', 'second'].entries()) {
  const fieldHandle = { stateNode: { node: null }, name: name };
  const left = 40 + index * 380;

  fieldHandle.stateNode.node = fabric.createNode(
    10 + index,
    'TextInput',
    surfaceId,
    Object.assign(style(left), { placeholder: name }),
    fieldHandle,
  );

  handles.push(fieldHandle);
  fabric.appendChild(container, fieldHandle.stateNode.node);
}

fabric.registerEventHandler((instanceHandle, type, payload) => {
  const name = instanceHandle === null || instanceHandle === undefined ? 'unknown' : instanceHandle.name;
  const text = payload !== null && payload !== undefined && payload.text !== undefined ? payload.text : '';

  console.log('text-input-compose: ' + type + ' on ' + name + ' text="' + text + '"');
});

const rootChildren = fabric.createChildSet();

fabric.appendChildToSet(rootChildren, container);
fabric.completeRoot(surfaceId, rootChildren);

console.log('text-input-compose: committed surface ' + surfaceId);
