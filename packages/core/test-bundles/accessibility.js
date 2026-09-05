// The fixture the accessibility projection of issue #216 is asked about: one node per rule the projection has,
// so the checked-in snapshot fails if any of them changes.
//
// Every node carries a testID or a nativeID, because neither an accessibilityRole nor a label nor a value forms
// a view on its own — Fabric would flatten a node carrying only those away before it ever reached the mounting
// layer, and the projection can only report nodes that mounted. For the same reason the tree the snapshot holds
// is shallower than the one written here: a node that forms a view but not a stacking context has its children
// hoisted to the nearest ancestor that does, which is `panel` losing all of its.
//
// rnl_window --automation --fabric packages/core/test-bundles/accessibility.js

const surfaceId = 1;
const fabric = globalThis.nativeFabricUIManager;

if (fabric === undefined) {
  throw new Error('nativeFabricUIManager was not installed');
}

// C++ holds instance handles weakly, so React retains them on its fibers. Keep them alive for the same reason.
const instanceHandles = [];

const createNode = (tag, componentName, props) => {
  const instanceHandle = {};

  instanceHandles.push(instanceHandle);

  return fabric.createNode(tag, componentName, surfaceId, props, instanceHandle);
};

const createParagraph = (tag, rawTextTag, text, props) => {
  const paragraph = createNode(tag, 'Paragraph', { color: 0xfff2f4f8 | 0, fontSize: 16, ...props });

  fabric.appendChild(paragraph, createNode(rawTextTag, 'RawText', { text }));

  return paragraph;
};

const box = (left, top) => ({ height: 24, left, position: 'absolute', top, width: 200 });

const panel = createNode(2, 'View', {
  backgroundColor: 0xff1e2430 | 0,
  height: 320,
  left: 20,
  position: 'absolute',
  testID: 'panel',
  top: 20,
  width: 240,
});

// Exposed: an authored accessibilityRole, a label, a hint and one state flag, with a paragraph under it that the
// projection keeps as a child because `accessible` makes this node a stacking context.
const sendButton = createNode(3, 'View', {
  accessibilityHint: 'Sends the message',
  accessibilityLabel: 'Send',
  accessibilityRole: 'button',
  accessibilityState: { disabled: true },
  accessible: true,
  nativeID: 'send',
  ...box(0, 0),
});

fabric.appendChild(sendButton, createParagraph(4, 101, 'Send now', box(0, 0)));

// Exposed: the ARIA `role` prop, which upstream's own toString names, plus a value and a labelledBy relation the
// projection resolves to the tag carrying that nativeID.
const slider = createNode(5, 'View', {
  accessibilityLabelledBy: ['caption'],
  accessibilityValue: { max: 10, min: 0, now: 4, text: 'four' },
  role: 'slider',
  testID: 'slider',
  ...box(0, 32),
});

// Exposed as text named by what it laid out, with no accessibility prop at all.
const caption = createParagraph(6, 102, 'Volume', { nativeID: 'caption', ...box(0, 64) });

// Exposed with every accessibilityState member the projection reports.
const checkbox = createNode(7, 'View', {
  accessibilityRole: 'checkbox',
  accessibilityState: { busy: true, checked: 'mixed', expanded: true, selected: true },
  testID: 'checkbox',
  ...box(0, 96),
});

// Pruned with its subtree: accessibilityElementsHidden.
const hiddenGroup = createNode(8, 'View', { accessibilityElementsHidden: true, testID: 'hidden', ...box(0, 128) });

fabric.appendChild(
  hiddenGroup,
  createNode(9, 'View', { accessibilityLabel: 'Never spoken', accessible: true, ...box(0, 0) }),
);

// Skipped alone: importantForAccessibility "no" drops the node and keeps the subtree in its place.
const decorative = createNode(10, 'View', {
  accessibilityLabel: 'Decoration',
  accessible: true,
  importantForAccessibility: 'no',
  ...box(0, 160),
});

fabric.appendChild(decorative, createNode(11, 'View', { accessibilityLabel: 'Kept', accessible: true, ...box(0, 0) }));

// Exposed although it is a plain box: importantForAccessibility "yes".
const forced = createNode(12, 'View', { importantForAccessibility: 'yes', ...box(0, 192) });

// Pruned with its subtree: importantForAccessibility "no-hide-descendants".
const noDescendants = createNode(13, 'View', { importantForAccessibility: 'no-hide-descendants', ...box(0, 224) });

fabric.appendChild(noDescendants, createNode(14, 'View', { accessibilityLabel: 'Buried', accessible: true, ...box(0, 0) }));

for (const child of [sendButton, slider, caption, checkbox, hiddenGroup, decorative, forced, noDescendants]) {
  fabric.appendChild(panel, child);
}

const rootChildren = fabric.createChildSet();

fabric.appendChildToSet(rootChildren, panel);
fabric.completeRoot(surfaceId, rootChildren);

globalThis.__rnlMarkTestPassed();

console.log('accessibility: committed surface 1');
