// The auto-grow fixture for issue #114, and core#54570, core#52854 and core#46813: a multiline <TextInput> with no
// height of its own, above a sibling that sits wherever the field's bottom edge is. Typing lines into the field
// makes it taller, the sibling moves down with it, and every change of height reaches JavaScript as one
// `onContentSizeChange` carrying the new size — after the `onChange` for the text that caused it and before the
// `onSelectionChange` for the caret that followed.
//
// hello_react --type packages/core/test-bundles/text-input-grow.js /tmp/grow-first.png ""
// hello_react --type packages/core/test-bundles/text-input-grow.js /tmp/grow-after.png "one{Enter}two{Enter}three"

const surfaceId = 1;
const fabric = globalThis.nativeFabricUIManager;

if (fabric === undefined) {
  throw new Error('nativeFabricUIManager was not installed');
}

const instances = [];

const mount = (tag, componentName, label, props, children) => {
  const instance = { name: label, stateNode: { node: null } };
  const node = fabric.createNode(tag, componentName, surfaceId, props, instance);

  instance.stateNode.node = node;
  instances.push(instance);
  (children || []).forEach((child) => fabric.appendChild(node, child));

  return node;
};

const grow = mount(10, 'TextInput', 'grow', {
  multiline: true,
  minHeight: 44,
  margin: 40,
  marginBottom: 0,
  padding: 8,
  paddingHorizontal: 12,
  borderWidth: 2,
  borderRadius: 10,
  borderColor: 0xffe5c07b | 0,
  backgroundColor: 0xff1e2430 | 0,
  color: 0xfff2f4f8 | 0,
  fontSize: 18,
  accessible: true,
  cursorColor: 0xff98c379 | 0,
  placeholder: 'Grows as you type',
  placeholderTextColor: 0xff6b7280 | 0,
  mostRecentEventCount: 0,
});

// The sibling is the picture of the growth: it starts under a one-line field and ends under a three-line one.
const follower = mount(11, 'View', 'follower', {
  marginLeft: 40,
  marginRight: 40,
  marginTop: 12,
  height: 40,
  backgroundColor: 0xff3366cc | 0,
});

const column = mount(2, 'View', 'column', { flex: 1 }, [grow, follower]);

let lastHeight = 0;

const describe = (payload) => {
  if (payload === null || payload === undefined) {
    return '';
  }

  const text = payload.text === undefined ? '' : ' text="' + payload.text.replace(/\n/g, '\\n') + '"';

  if (payload.contentSize === undefined) {
    return text;
  }

  const height = payload.contentSize.height;
  const verdict = height > lastHeight ? ' grew' : ' same-or-shrank';

  lastHeight = height;

  return text + ' height=' + height + verdict;
};

fabric.registerEventHandler((instanceHandle, type, payload) => {
  const name = instanceHandle === null || instanceHandle === undefined ? 'unknown' : instanceHandle.name;

  console.log('text-input-grow: ' + type + ' on ' + name + describe(payload));
});

const rootChildren = fabric.createChildSet();

fabric.appendChildToSet(rootChildren, column);
fabric.completeRoot(surfaceId, rootChildren);

console.log('text-input-grow: committed surface ' + surfaceId);
