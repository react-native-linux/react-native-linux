// The paragraph-direction fixture for issue #72: the same Latin text under each way a paragraph gets its base
// direction. In an RTL paragraph natural alignment is the right edge and the bidi algorithm puts the trailing "!"
// on the visual left, so the picture shows the direction without needing an RTL script's font.
//
//   1. LTR, the default.
//   2. writingDirection: 'rtl'.
//   3. A parent with direction: 'rtl', the paragraph's own direction natural.
//   4. writingDirection: 'ltr' inside that RTL parent, which the explicit direction wins.
//   5. A wrapped RTL paragraph: every line on the right edge, the full stop on the left of the last.

const surfaceId = 1;
const fabric = globalThis.nativeFabricUIManager;
const instanceHandles = [];
const panel = 0xff1e2430 | 0;

let nextTag = 2;

const node = (componentName, props) => {
  const handle = {};

  instanceHandles.push(handle);
  nextTag += 1;

  return fabric.createNode(nextTag, componentName, surfaceId, props, handle);
};

const paragraph = (text, props = {}) => {
  const created = node('Paragraph', { color: 0xfff2f4f8 | 0, fontSize: 20, ...props });

  fabric.appendChild(created, node('RawText', { text }));

  return created;
};

const row = (top, direction, child) => {
  const created = node('View', { backgroundColor: panel, direction, left: 40, position: 'absolute', top, width: 360 });

  fabric.appendChild(created, child);

  return created;
};

const rows = [
  row(40, 'ltr', paragraph('Hello, world!')),
  row(100, 'ltr', paragraph('Hello, world!', { writingDirection: 'rtl' })),
  row(160, 'rtl', paragraph('Hello, world!')),
  row(220, 'rtl', paragraph('Hello, world!', { writingDirection: 'ltr' })),
  row(280, 'ltr', paragraph('The quick brown fox jumps over the lazy dog, again and again.', { writingDirection: 'rtl' })),
];

const rootChildren = fabric.createChildSet();

for (const created of rows) {
  fabric.appendChildToSet(rootChildren, created);
}

fabric.completeRoot(surfaceId, rootChildren);

console.log('rtl-text: committed surface ' + surfaceId);
