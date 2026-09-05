// The placeholder proof for issue #255: painted with the field's own font, weight, letter spacing and
// alignment — never its own — with only its colour substituted, and the caret of an untouched field indexes the
// placeholder's own first glyph because there is no second paragraph to measure it against.
//
// hello_react --type packages/core/test-bundles/text-input-placeholder.js /tmp/rnl-placeholder.png "{Tab}"
// hello_react --type packages/core/test-bundles/text-input-placeholder.js /tmp/rnl-placeholder.png "{Tab}H"
//
// Four fields, none of them ever typed into but the first: left, centre and right `textAlign`, all bold,
// letter-spaced and italic, and a fifth multiline field whose placeholder is long enough to wrap. One Tab
// focuses the left field, so its caret is the one the pair of goldens above compares before and after the first
// character — the other three prove the style and alignment matrix without needing a caret of their own.

const surfaceId = 1;
const fabric = globalThis.nativeFabricUIManager;

if (fabric === undefined) {
  throw new Error('nativeFabricUIManager was not installed');
}

// Bold, letter-spaced and italic: three attributes a placeholder has historically ignored (core#50137,
// core#45853, core#42589), on top of `textAlign`, which the caret has historically gotten wrong (core#38528,
// core#41105). Every field shares this base and only its position, alignment, multiline-ness and placeholder
// string vary, so the matrix is a table of overrides rather than four near-identical prop objects.
const sharedTextStyle = {
  color: 0xfff2f4f8 | 0,
  fontSize: 20,
  fontWeight: 'bold',
  fontStyle: 'italic',
  letterSpacing: 3,
  accessible: true,
  cursorColor: 0xff98c379 | 0,
  placeholderTextColor: 0xffe06c75 | 0,
};

const sharedFieldFrame = {
  paddingLeft: 12,
  paddingRight: 12,
  paddingTop: 8,
  paddingBottom: 8,
  borderWidth: 2,
  borderRadius: 10,
  borderColor: 0xffe5c07b | 0,
  backgroundColor: 0xff1e2430 | 0,
};

const fieldSpecs = [
  { tag: 10, name: 'left', left: 40, top: 40, width: 320, height: 56, textAlign: 'left', placeholder: 'Type here' },
  {
    tag: 11,
    name: 'center',
    left: 400,
    top: 40,
    width: 320,
    height: 56,
    textAlign: 'center',
    placeholder: 'Type here',
  },
  { tag: 12, name: 'right', left: 40, top: 120, width: 680, height: 56, textAlign: 'right', placeholder: 'Type here' },
  {
    tag: 13,
    name: 'multiline',
    left: 40,
    top: 200,
    width: 320,
    height: 140,
    textAlign: 'left',
    multiline: true,
    placeholder: 'A placeholder long enough that it wraps onto several lines of a narrow field, exactly as typed text would.',
  },
];

const container = fabric.createNode(2, 'View', surfaceId, { flex: 1 }, { name: 'container', stateNode: {} });
const rootChildren = fabric.createChildSet();

fabric.registerEventHandler(() => {});

for (const spec of fieldSpecs) {
  const handle = { name: spec.name, stateNode: { node: null } };
  const props = Object.assign({}, sharedTextStyle, sharedFieldFrame, {
    position: 'absolute',
    left: spec.left,
    top: spec.top,
    width: spec.width,
    height: spec.height,
    textAlign: spec.textAlign,
    multiline: spec.multiline === true,
    placeholder: spec.placeholder,
  });
  const node = fabric.createNode(spec.tag, 'TextInput', surfaceId, props, handle);

  handle.stateNode.node = node;
  fabric.appendChild(container, node);
}

fabric.appendChildToSet(rootChildren, container);
fabric.completeRoot(surfaceId, rootChildren);

console.log('text-input-placeholder: committed surface ' + surfaceId);
