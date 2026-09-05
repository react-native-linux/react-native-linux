// The colour-emoji fixture for the golden-image rig. Issue #249; docs/cpp-toolchain.md *Colour emoji and the
// fallback chain (#249)* states the policy this picture proves.
//
// The vendored Noto Sans carries no emoji at all, so every row below the control row is a codepoint the primary
// face lacks and a fallback face has to supply. The four fallback rows are the four ways that can go wrong:
//
//   - a single emoji codepoint, which is the plain CBDT bitmap glyph;
//   - a regional-indicator pair, which is one flag glyph, or two letters if shaping split the pair across runs;
//   - a ZWJ family sequence, which is one glyph if the ligature survived the run split and three if it did not;
//   - a skin-tone modifier, which is one recoloured glyph, or a hand followed by a colour swatch.
//
// Latin sits either side of the emoji in every row, so the run splits out of the primary face and back into it.
//
// Each row is drawn three ways. The left column is the measured-frame-over-line-box sandwich of text-metrics.js —
// the <View> background is the height Yoga was given, the <Text> fragment background is the line box SkParagraph
// painted — at the natural line height and then at an explicit lineHeight of 24. That is react-native#47621: a
// fallback face with its own ascent and descent must not make the line box taller than the Latin control row's.
// The right column is the same string at 40 points, because a bitmap face is scaled rather than outlined.
//
// The emoji face is the Noto Color Emoji pinned in scripts/fonts.lock.json, not whatever fontconfig finds, which
// is what keeps this a golden: without it a machine with no system emoji font draws tofu boxes.

const surfaceId = 1;
const fabric = globalThis.nativeFabricUIManager;

if (fabric === undefined) {
  throw new Error('nativeFabricUIManager was not installed');
}

// C++ holds instance handles weakly, so React retains them on its fibers. Keep them alive for the same reason.
const instanceHandles = [];

const nextTag = () => instanceHandles.length + 2;

const createNode = (componentName, props, children = []) => {
  const instanceHandle = {};
  const node = fabric.createNode(nextTag(), componentName, surfaceId, props, instanceHandle);

  instanceHandles.push(instanceHandle);

  for (const child of children) {
    fabric.appendChild(node, child);
  }

  return node;
};

const rawText = (value) => createNode('RawText', { text: value });
const text = (props, children) => createNode('Text', props, children);
const paragraph = (props, children) => createNode('Paragraph', props, children);
const view = (props, children) => createNode('View', props, children);

const bodyColor = 0xfff2f4f8 | 0;
const captionColor = 0xff9aa4b2 | 0;
const measuredFrame = 0xff1e2430 | 0;
const lineBox = 0xff34405a | 0;

const rows = [
  ['latin control', 'ok none done'],
  ['single codepoint', 'ok \u{1F600} done'],
  ['regional indicators', 'flag \u{1F1FA}\u{1F1E6} end'],
  ['zero-width joiner', 'family \u{1F468}\u{200D}\u{1F469}\u{200D}\u{1F467} end'],
  ['skin-tone modifier', 'wave \u{1F44B}\u{1F3FF} end'],
];

const bodyFontSize = 18;
const largeFontSize = 40;
const explicitLineHeight = 24;
const rowPitch = 108;
const firstRowTop = 40;
const captionHeight = 18;
const sandwichPitch = 40;
const leftColumnLeft = 40;
const leftColumnWidth = 380;
const rightColumnLeft = 440;
const rightColumnWidth = 340;

const label = (left, top, width, caption) =>
  paragraph({ color: captionColor, fontSize: 12, left, position: 'absolute', top, width }, [rawText(caption)]);

// The line box is the <Text> fragment's background and the measured frame is the <View>'s, so a fallback run that
// inflates the line shows as a lineBox band taller than the control row's.
const sandwich = (left, top, width, body, lineHeight) =>
  view({ backgroundColor: measuredFrame, left, position: 'absolute', top, width }, [
    paragraph({ color: bodyColor, fontSize: bodyFontSize, ...(lineHeight === null ? {} : { lineHeight }) }, [
      text({ backgroundColor: lineBox }, [rawText(body)]),
    ]),
  ]);

const emojiRow = ([caption, body], index) => {
  const top = firstRowTop + (index * rowPitch);

  return [
    label(leftColumnLeft, top, leftColumnWidth, caption),
    sandwich(leftColumnLeft, top + captionHeight, leftColumnWidth, body, null),
    sandwich(leftColumnLeft, top + captionHeight + sandwichPitch, leftColumnWidth, body, explicitLineHeight),
    label(rightColumnLeft, top, rightColumnWidth, `${caption}, 40 points`),
    paragraph(
      {
        color: bodyColor,
        fontSize: largeFontSize,
        left: rightColumnLeft,
        position: 'absolute',
        top: top + captionHeight,
        width: rightColumnWidth,
      },
      [rawText(body)],
    ),
  ];
};

const root = view({ flex: 1 }, rows.flatMap(emojiRow));

const rootChildren = fabric.createChildSet();

fabric.appendChildToSet(rootChildren, root);
fabric.completeRoot(surfaceId, rootChildren);

console.log('emoji: committed surface ' + surfaceId);
