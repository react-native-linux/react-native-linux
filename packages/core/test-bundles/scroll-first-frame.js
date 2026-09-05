// The first-frame fixture for issue #46, and rn-macos#2857: a large <Text> inside a ScrollView that produced
// "massive vertical and horizontal overflow on the first render", repaired only by resizing the window. The shape
// is always a content size computed from a measurement that was not yet final and never recomputed.
//
// One ScrollView holding the three things whose measurement can settle late: a paragraph that wraps onto many
// lines, a column of images whose decodes finish after the mount, and a nested ScrollView with its own content.
// `--first-frame-golden` snapshots the scene at the first commit and again once everything has settled, and the
// geometry of the two has to be the same — that single equality is the whole of the bug.
//
// hello_react --first-frame-golden packages/core/test-bundles/scroll-first-frame.js /tmp/first-frame.png

const surfaceId = 1;
const fabric = globalThis.nativeFabricUIManager;

if (fabric === undefined) {
  throw new Error('nativeFabricUIManager was not installed');
}

const retained = [];
let tag = 2;

const make = (componentName, props, children = []) => {
  const handle = {};
  const node = fabric.createNode(tag, componentName, surfaceId, props, handle);

  tag += 1;
  retained.push(handle);
  children.forEach((child) => fabric.appendChild(node, child));

  return node;
};

const words = 'the content size a scrollable derives from a measurement that is not yet final is the content size it keeps until something unrelated makes it look again ';
const longParagraph = make('Paragraph', { color: 0xfff2f4f8 | 0, fontSize: 16, lineHeight: 22 }, [
  make('RawText', { text: words.repeat(2) }),
]);

const imageColumn = [0, 1, 2].map((index) =>
  make('Image', {
    width: 120,
    height: 80,
    marginTop: 8,
    source: { uri: 'rnl-test-image.png' },
    resizeMode: 'cover',
    backgroundColor: 0xff1e2430 | 0,
  }),
);

const nested = make(
  'ScrollView',
  { height: 90, marginTop: 8, backgroundColor: 0xff2a3142 | 0 },
  [0, 1, 2, 3].map((index) =>
    make('View', { height: 40, marginTop: 4, backgroundColor: [0xffe06c75, 0xff98c379, 0xff61afef, 0xffe5c07b][index] | 0 }),
  ),
);

const scrollView = make(
  'ScrollView',
  { position: 'absolute', left: 40, top: 40, width: 360, height: 400, backgroundColor: 0xff11141a | 0 },
  [longParagraph, ...imageColumn, nested],
);

// Directly below the viewport, as in scroll.js: content that overflowed the ScrollView on the first frame would
// land on it.
const marker = make('View', {
  position: 'absolute',
  left: 40,
  top: 460,
  width: 360,
  height: 40,
  backgroundColor: 0xff3366cc | 0,
});

const container = make('View', { flex: 1 }, [scrollView, marker]);
const rootChildren = fabric.createChildSet();

fabric.appendChildToSet(rootChildren, container);
fabric.completeRoot(surfaceId, rootChildren);

console.log('scroll-first-frame: committed surface ' + surfaceId);
