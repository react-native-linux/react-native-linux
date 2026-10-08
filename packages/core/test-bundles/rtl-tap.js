// Issue #343's e2e: tapping the visually leftmost glyph of a right-to-left word places the caret on the correct
// character. The field holds one Hebrew word, left-aligned, so its leftmost glyph is the word's *last* letter, ם at
// offset 3, starting at the field's padding. The scenario taps that glyph twice: in its right half, which is the
// leading edge of a right-to-left letter and so offset 3, then in its left half, its trailing edge, offset 4. A
// left-to-right reading of the box gets both of them backwards.

const surfaceId = 1;
const fabric = globalThis.nativeFabricUIManager;

if (fabric === undefined) {
  throw new Error('nativeFabricUIManager was not installed');
}

const field = { stateNode: { node: null } };

field.stateNode.node = fabric.createNode(
  2,
  'TextInput',
  surfaceId,
  {
    position: 'absolute',
    left: 40,
    top: 40,
    width: 300,
    height: 44,
    padding: 10,
    textAlign: 'left',
    color: 0xfff2f4f8 | 0,
    backgroundColor: 0xff1e2430 | 0,
    fontSize: 18,
    accessible: true,
    text: 'שלום',
    mostRecentEventCount: 0,
  },
  field,
);

const children = fabric.createChildSet();

fabric.appendChildToSet(children, field.stateNode.node);
fabric.completeRoot(surfaceId, children);

// The mount reports a selection of its own, at the end of the text; only the ones after the tap focused the field
// say where the tap put the caret.
let isFocused = false;

fabric.registerEventHandler((instanceHandle, type, payload) => {
  if (type === 'topFocus') {
    isFocused = true;
  }

  if (isFocused && type === 'topSelectionChange' && payload.selection !== undefined) {
    console.log('rtl-tap: caret ' + payload.selection.start + '..' + payload.selection.end);
  }
});

console.log('rtl-tap: committed surface ' + surfaceId);
