// The <TextInput> proof for issue #17, and the fixture behind the parity matrix of #53 and the key contract of
// #54. Three fields in tab order: a plain single-line one, a secureTextEntry one that already has a value, and a
// multiline one whose value wraps.
//
// hello_react --type packages/core/test-bundles/text-input.js /tmp/rnl-typing.png "Hello{Left}{Left}X"
// hello_react --type packages/core/test-bundles/text-input.js /tmp/rnl-selection.png "Hello world{Ctrl+A}"
//
// One Tab focuses the first field, so everything typed after it goes there. The others are never typed into:
// the secure one proves that the buffer is masked before it reaches a paragraph — the picture shows bullets and
// the paragraph never contains anything else — and the multiline one proves wrapping and a caret that is not on
// the first line.

const surfaceId = 1;
const fabric = globalThis.nativeFabricUIManager;

if (fabric === undefined) {
  throw new Error('nativeFabricUIManager was not installed');
}

const handles = [];

const createNode = (tag, componentName, name, props) => {
  const handle = { name: name, stateNode: { node: null } };
  const node = fabric.createNode(tag, componentName, surfaceId, props, handle);

  handle.stateNode.node = node;
  handles.push(handle);

  return node;
};

const container = createNode(2, 'View', 'container', { flex: 1 });

const fieldStyle = (left, top, width, height) => ({
  position: 'absolute',
  left: left,
  top: top,
  width: width,
  height: height,
  paddingLeft: 12,
  paddingRight: 12,
  paddingTop: 8,
  paddingBottom: 8,
  borderWidth: 2,
  borderRadius: 10,
  borderColor: 0xffe5c07b | 0,
  backgroundColor: 0xff1e2430 | 0,
  color: 0xfff2f4f8 | 0,
  fontSize: 18,
  accessible: true,
  cursorColor: 0xff98c379 | 0,
  selectionColor: 0x5961afef | 0,
  placeholderTextColor: 0xff6b7280 | 0,
});

const plain = createNode(
  10,
  'TextInput',
  'plain',
  Object.assign(fieldStyle(40, 40, 340, 48), { placeholder: 'Type here' }),
);

const secure = createNode(
  11,
  'TextInput',
  'secure',
  Object.assign(fieldStyle(420, 40, 340, 48), {
    secureTextEntry: true,
    text: 'hunter2',
    mostRecentEventCount: 0,
  }),
);

const multiline = createNode(
  12,
  'TextInput',
  'multiline',
  Object.assign(fieldStyle(40, 120, 720, 140), {
    multiline: true,
    fontSize: 16,
    text: 'A multiline field wraps its value onto as many lines as it needs, and the caret follows the line it is on.',
    mostRecentEventCount: 0,
  }),
);

// #53, case 4, and rn-macos#1395: a caret whose height is a constant looks right at one font size and wrong at
// every other one. These four fields are never focused and never typed into — the picture is their text, and the
// assertion is `--type`'s own, which asks each field's paragraph how tall the line under its caret is.
const sized = (tag, name, left, top, width, height, textAttributes, value) =>
  createNode(
    tag,
    'TextInput',
    name,
    Object.assign(fieldStyle(left, top, width, height), textAttributes, { text: value, mostRecentEventCount: 0 }),
  );

const twelvePoint = sized(13, 'twelve', 40, 290, 220, 40, { fontSize: 12 }, 'Twelve point');
const twentyFourPoint = sized(14, 'twenty-four', 280, 290, 240, 56, { fontSize: 24 }, 'Twenty four');
const fortyPoint = sized(15, 'forty', 540, 290, 220, 80, { fontSize: 40 }, 'Forty');
const tallLine = sized(
  16,
  'tall-line',
  40,
  390,
  720,
  70,
  { fontSize: 16, lineHeight: 40 },
  'A forty point line height on a sixteen point font.',
);

// The single JavaScript entry point for every Fabric event. React's renderer installs its own dispatcher here;
// this one reports, so the printed order is the order the C++ side produced.
fabric.registerEventHandler((instanceHandle, type, payload) => {
  const name = instanceHandle === null || instanceHandle === undefined ? 'unknown' : instanceHandle.name;
  const parts = [];

  if (payload !== null && payload !== undefined) {
    if (payload.text !== undefined) {
      parts.push('text="' + payload.text + '"');
    }

    if (payload.key !== undefined) {
      parts.push('key=' + payload.key);
    }

    if (payload.selection !== undefined) {
      parts.push('selection=' + payload.selection.start + '..' + payload.selection.end);
    }

    if (payload.eventCount !== undefined) {
      parts.push('eventCount=' + payload.eventCount);
    }
  }

  const suffix = parts.length === 0 ? '' : ' ' + parts.join(' ');

  console.log('text-input: ' + type + ' on ' + name + suffix);
});

fabric.appendChild(container, plain);
fabric.appendChild(container, secure);
fabric.appendChild(container, multiline);
fabric.appendChild(container, twelvePoint);
fabric.appendChild(container, twentyFourPoint);
fabric.appendChild(container, fortyPoint);
fabric.appendChild(container, tallLine);

const rootChildren = fabric.createChildSet();

fabric.appendChildToSet(rootChildren, container);
fabric.completeRoot(surfaceId, rootChildren);

console.log('text-input: committed surface ' + surfaceId);
