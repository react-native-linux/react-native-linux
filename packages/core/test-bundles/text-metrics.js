// The vertical-metrics fixture for the golden-image rig: what `lineHeight` does to a line box, drawn so the box
// is visible. Issue #110; docs/cpp-toolchain.md *Vertical metrics (#110)* states the policy this picture proves.
//
// Every row is a <View> with one background under a <Text> fragment with another: the view is the height Yoga was
// given by TextLayoutManager::measure, and the fragment background is the line box SkParagraph painted. Where the
// two disagree, the picture shows it — that is issue #41's equality, vertically.
//
// The strings are ASCII plus codepoints the vendored Noto Sans actually carries, because anything else resolves
// through fontconfig and stops being reproducible. Noto Sans covers Latin, Latin Extended, Greek and Cyrillic and
// nothing else, so the tall-ascender row is U+01FA U+01F0 U+1E9E (Latin Extended), U+038F U+03BE (Greek) and
// U+0402 U+045E (Cyrillic). Devanagari, Arabic and Tibetan — react-native#33704 — need their own vendored faces
// and are a follow-up on #110, not an approximation to make here.

const surfaceId = 1;
const fabric = globalThis.nativeFabricUIManager;

if (fabric === undefined) {
  throw new Error('nativeFabricUIManager was not installed');
}

// C++ holds instance handles weakly, so React retains them on its fibers. Keep them alive for the same reason.
const instanceHandles = [];

const createNode = (componentName, props, children = []) => {
  const handle = {};
  const node = fabric.createNode(instanceHandles.length + 2, componentName, surfaceId, props, handle);

  instanceHandles.push(handle);
  children.forEach((child) => fabric.appendChild(node, child));

  return node;
};

const component = (componentName) => (props, children = []) => createNode(componentName, props, children);
const rawText = (value) => createNode('RawText', { text: value });
const [text, paragraph, view] = ['Text', 'Paragraph', 'View'].map(component);

const white = 0xfff2f4f8 | 0;
const muted = 0xff9aa4b2 | 0;
const measuredFrame = 0xff1e2430 | 0;
const lineBox = 0xff34405a | 0;

const descenders = 'Hxg gjpqy';
const wrapping = 'letter spacing keeps the line count';
const tallScripts = 'Ǻǰẞ Ώξ Ђў';

const leftColumnLeft = 40;
const leftColumnWidth = 320;
const rightColumnLeft = 420;
const rightColumnWidth = 260;
const labelHeight = 18;
const bodyFontSize = 28;

const label = (left, top, width, caption) =>
  paragraph({ position: 'absolute', left, top, width, color: muted, fontSize: 12 }, [rawText(caption)]);

// The line box is the <Text> fragment's background and the measured frame is the <View>'s, so a lineHeight that
// does not match the font's own ascent plus descent shows as measuredFrame above and below the lineBox band.
const metricsRow = (left, top, width, caption, textProps, content) => [
  label(left, top, width, caption),
  view({ position: 'absolute', left, top: top + labelHeight, width, backgroundColor: measuredFrame }, [
    paragraph({ color: white, ...textProps }, [text({ backgroundColor: lineBox }, [rawText(content)])]),
  ]),
];

const heading = paragraph(
  { position: 'absolute', left: 40, top: 24, width: 720, color: white, fontSize: 24, fontWeight: 'bold' },
  [rawText('Vertical metrics: lineHeight and half leading')],
);

const rows = [
  ...metricsRow(
    leftColumnLeft,
    76,
    leftColumnWidth,
    'fontSize 28, no lineHeight',
    { fontSize: bodyFontSize },
    descenders,
  ),
  ...metricsRow(
    leftColumnLeft,
    168,
    leftColumnWidth,
    'fontSize 28, lineHeight 28 (leading below 0)',
    { fontSize: bodyFontSize, lineHeight: 28 },
    descenders,
  ),
  ...metricsRow(
    leftColumnLeft,
    260,
    leftColumnWidth,
    'fontSize 28, lineHeight 19.6 (overflow)',
    { fontSize: bodyFontSize, lineHeight: 19.6 },
    descenders,
  ),
  ...metricsRow(
    leftColumnLeft,
    352,
    leftColumnWidth,
    'fontSize 28, lineHeight 56 (even leading)',
    { fontSize: bodyFontSize, lineHeight: 56 },
    descenders,
  ),
  ...metricsRow(
    leftColumnLeft,
    460,
    leftColumnWidth,
    'fontSize 28, lineHeight 28, tall accents',
    { fontSize: bodyFontSize, lineHeight: 28 },
    tallScripts,
  ),
  ...metricsRow(
    rightColumnLeft,
    76,
    rightColumnWidth,
    'fontSize 16, lineHeight 24',
    { fontSize: 16, lineHeight: 24 },
    wrapping,
  ),
  ...metricsRow(
    rightColumnLeft,
    168,
    rightColumnWidth,
    'the same, letterSpacing 2',
    { fontSize: 16, letterSpacing: 2, lineHeight: 24 },
    wrapping,
  ),
  ...metricsRow(
    rightColumnLeft,
    280,
    rightColumnWidth,
    'fontSize 20, lineHeight 40, wrapped',
    { fontSize: 20, lineHeight: 40 },
    'first and last line boxes match the interior ones',
  ),
];

const root = view({ flex: 1 }, [heading, ...rows]);

const rootChildren = fabric.createChildSet();

fabric.appendChildToSet(rootChildren, root);
fabric.completeRoot(surfaceId, rootChildren);

console.log('text-metrics: committed surface ' + surfaceId);
