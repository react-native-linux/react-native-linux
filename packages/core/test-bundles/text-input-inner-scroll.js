// The inner-scrolling fixture for issue #256 and react/core#49226: a fixed-height multiline <TextInput> inside a
// <ScrollView>, with a tall sibling under it so the outer view has somewhere to scroll to. The field is five
// lines tall and its value is fifteen, so both viewports are real and the tug-of-war between them is visible.
//
// hello_react --type packages/core/test-bundles/text-input-inner-scroll.js /tmp/bottom.png "{Ctrl+A}{Right}"
// hello_react --type packages/core/test-bundles/text-input-inner-scroll.js /tmp/top.png "{Ctrl+A}{Left}"
//
// One Tab focuses the field, so `{Ctrl+A}{Right}` collapses the selection to the end of the value and
// `{Ctrl+A}{Left}` to its start. Those are the two ends of the field's own window, reached without a caret
// motion the editing model does not have — Up, Down and Ctrl+Home are #17's, not this fixture's.

const fabric = globalThis.nativeFabricUIManager;

if (fabric === undefined) {
  throw new Error('nativeFabricUIManager was not installed');
}

const surfaceId = 1;
// Kept alive because Fabric holds instance handles weakly; each one is the name the event trace prints.
const named = [];

const createNode = (tag, componentName, name, props) => {
  const handle = { name: name, stateNode: { node: null } };

  handle.stateNode.node = fabric.createNode(tag, componentName, surfaceId, props, handle);
  named.push(handle);

  return handle.stateNode.node;
};

const line = (index) => 'Line ' + index + ' of a value that is far taller than the box it is drawn in.';
const value = [1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15].map(line).join('\n');

// Five lines of an eighteen point font plus its padding: fixed, so the field scrolls rather than growing.
const field = createNode(10, 'TextInput', 'inner', {
  multiline: true,
  height: 130,
  marginHorizontal: 40,
  marginTop: 40,
  paddingHorizontal: 12,
  paddingVertical: 8,
  borderWidth: 2,
  borderRadius: 10,
  borderColor: 0xffe5c07b | 0,
  backgroundColor: 0xff1e2430 | 0,
  color: 0xfff2f4f8 | 0,
  fontSize: 18,
  accessible: true,
  cursorColor: 0xff98c379 | 0,
  selectionColor: 0x5961afef | 0,
  text: value,
  mostRecentEventCount: 0,
});

// Taller than the surface on purpose: without it the outer <ScrollView> has no content to scroll and "the wheel
// moved the field and not its ancestor" would be true for the wrong reason.
const filler = createNode(11, 'View', 'filler', {
  marginHorizontal: 40,
  marginTop: 20,
  height: 900,
  backgroundColor: 0xff3366cc | 0,
});

const list = createNode(2, 'ScrollView', 'list', { flex: 1 });

fabric.appendChild(list, field);
fabric.appendChild(list, filler);

fabric.registerEventHandler((instanceHandle, type, payload) => {
  const name = instanceHandle === null || instanceHandle === undefined ? 'unknown' : instanceHandle.name;
  const parts = [];

  if (payload !== null && payload !== undefined && payload.contentOffset !== undefined) {
    parts.push('offsetY=' + Math.round(payload.contentOffset.y));
  }

  if (payload !== null && payload !== undefined && payload.selection !== undefined) {
    parts.push('selection=' + payload.selection.start + '..' + payload.selection.end);
  }

  const suffix = parts.length === 0 ? '' : ' ' + parts.join(' ');

  console.log('text-input-inner-scroll: ' + type + ' on ' + name + suffix);
});

const rootChildren = fabric.createChildSet();

fabric.appendChildToSet(rootChildren, list);
fabric.completeRoot(surfaceId, rootChildren);

console.log('text-input-inner-scroll: committed surface ' + surfaceId);
