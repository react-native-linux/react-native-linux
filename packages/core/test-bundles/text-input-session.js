// The `zwp_text_input_v3` session proof for issue #340: one field and one focusable node that is not a field, so
// focus moving between them is the whole of the enable and disable policy.
//
// hello_react --type packages/core/test-bundles/text-input-session.js /tmp/rnl-session.png "{Tab}{Tab}{Tab}"
//
// The trace lines the run is read for are the platform's own — `[rnl-ime] field focused purpose=email` when the
// field takes the caret and `[rnl-ime] field blurred` when the button does — because the session is what the
// compositor is told and the bundle cannot see it. A headless run and the e2e scenario print the same ones.

const surfaceId = 1;
const fabric = globalThis.nativeFabricUIManager;

if (fabric === undefined) {
  throw new Error('nativeFabricUIManager was not installed');
}

const retainedHandles = [];

const place = (left) => ({
  position: 'absolute',
  left: left,
  top: 40,
  width: 320,
  height: 48,
  borderRadius: 10,
  backgroundColor: 0xff1e2430 | 0,
  accessible: true,
});

const node = (tag, componentName, name, props) => {
  const instanceHandle = { name: name, stateNode: { node: null } };
  const created = fabric.createNode(tag, componentName, surfaceId, props, instanceHandle);

  instanceHandle.stateNode.node = created;
  retainedHandles.push(instanceHandle);

  return created;
};

const container = node(2, 'View', 'container', { flex: 1 });
const field = node(
  10,
  'TextInput',
  'address',
  Object.assign(place(40), {
    color: 0xfff2f4f8 | 0,
    fontSize: 18,
    keyboardType: 'email-address',
    placeholder: 'you@example.com',
    placeholderTextColor: 0xff6b7280 | 0,
  }),
);
const button = node(11, 'View', 'button', Object.assign(place(400), { onClick: true, onPointerDown: true }));

fabric.registerEventHandler((instanceHandle, type) => {
  const name = instanceHandle === null || instanceHandle === undefined ? 'unknown' : instanceHandle.name;

  console.log('text-input-session: ' + type + ' on ' + name);
});

fabric.appendChild(container, field);
fabric.appendChild(container, button);

const rootChildren = fabric.createChildSet();

fabric.appendChildToSet(rootChildren, container);
fabric.completeRoot(surfaceId, rootChildren);

console.log('text-input-session: committed surface ' + surfaceId);
