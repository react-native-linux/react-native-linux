// The height-parity fixture for issue #114, item 6: a `<TextInput>` and a `<Text>` holding the same string in
// the same style, neither given an explicit height of its own, so each one's box height is exactly its own
// measurement. `multiline` is required on the field for the comparison to mean anything: a single-line field
// measures unwrapped and scrolls, which is not the same question as a `<Text>`'s wrapped height. `--text-fit-golden`
// already visits every text primitive in the scene; the parity check itself —
// `doesEveryTextInputAgreeWithACompanionText` in `GoldenRenderer.cpp` — looks for a `<TextInput>`/`<Text>` pair
// with the same text and compares their committed heights, which is a silent no-op for every other fixture and
// the whole of the proof for this one.
//
// hello_react --text-fit-golden packages/core/test-bundles/text-input-text-height.js /tmp/text-height.png

const surfaceId = 1;
const fabric = globalThis.nativeFabricUIManager;

if (fabric === undefined) {
  throw new Error('nativeFabricUIManager was not installed');
}

const handles = [{}, {}, {}, {}];
const sharedStyle = { width: 260, fontSize: 22, color: 0xfff2f4f8 | 0 };
const sharedText = 'The same string in the same style measures to the same height.';

const field = fabric.createNode(
  10,
  'TextInput',
  surfaceId,
  Object.assign({ position: 'absolute', left: 40, top: 40, multiline: true, text: sharedText, mostRecentEventCount: 0 }, sharedStyle),
  handles[0],
);

const rawText = fabric.createNode(12, 'RawText', surfaceId, { text: sharedText }, handles[1]);
const paragraph = fabric.createNode(
  11,
  'Paragraph',
  surfaceId,
  Object.assign({ position: 'absolute', left: 40, top: 160 }, sharedStyle),
  handles[2],
);

fabric.appendChild(paragraph, rawText);

const container = fabric.createNode(2, 'View', surfaceId, { flex: 1 }, handles[3]);

fabric.appendChild(container, field);
fabric.appendChild(container, paragraph);

const rootChildren = fabric.createChildSet();

fabric.appendChildToSet(rootChildren, container);
fabric.completeRoot(surfaceId, rootChildren);

console.log('text-input-text-height: committed surface ' + surfaceId);
