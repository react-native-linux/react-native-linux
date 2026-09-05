// The first-frame fixture for issue #114, item 5, and core#54304: a fixed-height multiline field whose first
// line lands somewhere different once everything has settled than where it painted on the very first commit —
// the same shape as rn-macos#2857 (`scroll-first-frame.js`), turned on a `<TextInput>` instead of a `<Text>`.
//
// `--first-frame-golden` snapshots the scene at the first commit, with the second snapshot taken once the
// headless run has settled, and asserts every primitive's frame, matrix and clip frames are the same between the
// two. A field whose first line shifted between those two snapshots — because its measurement depended on
// something not yet final at the first commit — would fail here exactly as `scroll-first-frame.js` fails for a
// `<ScrollView>`.
//
// hello_react --first-frame-golden packages/core/test-bundles/text-input-first-frame.js /tmp/first-frame.png

const surfaceId = 1;
const fabric = globalThis.nativeFabricUIManager;

if (fabric === undefined) {
  throw new Error('nativeFabricUIManager was not installed');
}

const retained = [];

const make = (tag, componentName, props, children = []) => {
  const handle = {};
  const node = fabric.createNode(tag, componentName, surfaceId, props, handle);

  retained.push(handle);
  children.forEach((child) => fabric.appendChild(node, child));

  return node;
};

// Four lines long in a box exactly four lines tall, fixed rather than auto-growing: item 5 is about where the
// first line sits, not about whether the field resizes, which items 1-3 already cover.
const field = make(10, 'TextInput', {
  position: 'absolute',
  left: 40,
  top: 40,
  width: 360,
  height: 108,
  padding: 8,
  borderWidth: 2,
  borderRadius: 10,
  borderColor: 0xffe5c07b | 0,
  backgroundColor: 0xff1e2430 | 0,
  color: 0xfff2f4f8 | 0,
  fontSize: 16,
  lineHeight: 22,
  multiline: true,
  text: 'A fixed-height field whose first line must land in the same place on the first commit as it does once everything has settled.',
  mostRecentEventCount: 0,
});

// Directly below the field, as `scroll-first-frame.js` puts its marker below the ScrollView: a first line that
// shifted downward on settling would have pushed this marker's neighbourhood, even though the marker's own frame
// is fixed and cannot itself move — it is here so a scene that gained or lost a primitive between the two
// snapshots is not the only failure mode exercised.
const marker = make(11, 'View', {
  position: 'absolute',
  left: 40,
  top: 160,
  width: 360,
  height: 40,
  backgroundColor: 0xff3366cc | 0,
});

const container = make(2, 'View', { flex: 1 }, [field, marker]);
const rootChildren = fabric.createChildSet();

fabric.appendChildToSet(rootChildren, container);
fabric.completeRoot(surfaceId, rootChildren);

console.log('text-input-first-frame: committed surface ' + surfaceId);
