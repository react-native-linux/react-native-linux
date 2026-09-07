// The `ellipsizeMode` matrix for the golden-image rig: `head`, `middle`, `tail` and `clip`, each at
// `numberOfLines` 1 and 2. Issue #251; docs/cpp-toolchain.md *Text*, *Truncation that is not at the tail (#251)*
// describes what each row proves.
//
// `head` and `middle` are the searched ones: the text that survives is found by measuring candidates with the
// same shaper the paragraph is drawn with, so the cut lands on a grapheme boundary and the box still holds what
// was measured for it. The fifth row is the two things the search does not do: a token with no break opportunity
// inside it, which only `clip` cuts, and a paragraph carrying an inline attachment, which is left to the line
// limit. The last two rows are issue #312: one row per mode over a paragraph whose middle run is a nested
// `<Text>`, which truncates exactly as the unnested rows do because it is a styled fragment of the same
// paragraph rather than something embedded in it.
//
// Every string is ASCII, for the reason text.js is: anything outside the vendored Noto Sans resolves through
// fontconfig and stops being reproducible. The `letterSpacing` column is react/react-native#37511, where
// `middle` and letter spacing disagreed about where the text ended.

const surfaceId = 1;
const fabric = globalThis.nativeFabricUIManager;

if (fabric === undefined) {
  throw new Error('nativeFabricUIManager was not installed');
}

// Fabric holds instance handles weakly, so this array is what keeps them alive for as long as the bundle runs.
const handles = [];
let tag = 1;

const node = (componentName, props, children = []) => {
  const handle = {};

  handles.push(handle);
  tag += 1;

  const created = fabric.createNode(tag, componentName, surfaceId, props, handle);

  for (const child of children) {
    fabric.appendChild(created, child);
  }

  return created;
};

const rawText = (value) => node('RawText', { text: value });
// 'RCTVirtualText', not 'Text': componentNameByReactViewName rewrites 'Text' to 'Paragraph' (#312).
const text = (props, children) => node('RCTVirtualText', props, children);
const paragraph = (props, children) => node('Paragraph', props, children);
const view = (props, children) => node('View', props, children);

const white = 0xfff2f4f8 | 0;
const muted = 0xff9aa4b2 | 0;
const amber = 0xffe5c07b | 0;
const panel = 0xff1e2430 | 0;

const panelWidth = 360;
const panelLeft = 40;
const secondColumnLeft = 460;

function sentence() {
  return [
    rawText(
      'One line of prose that starts here, runs through the middle of the box, and then carries on for long ' +
        'enough that nothing on this surface can hold all of it at once.',
    ),
  ];
}

// The same prose with a nested <Text> in the middle of it: after #312 that run is a styled fragment of the one
// paragraph, so the line limit and the searched cuts apply across it exactly as they do to the prose around it.
function nestedSentence() {
  return [
    rawText('Prose with '),
    text({ color: amber, fontWeight: 'bold' }, [rawText('a nested Text run that keeps its own style')]),
    rawText(' and then plain prose that carries on well past the end of the box it was given.'),
  ];
}

function labelled(left, top, caption, paragraphProps, children) {
  const label = paragraph({ position: 'absolute', left: left, top: top, width: panelWidth, color: muted, fontSize: 13 }, [
    rawText(caption),
  ]);
  const box = view(
    {
      position: 'absolute',
      left: left,
      top: top + 20,
      width: panelWidth,
      backgroundColor: panel,
      padding: 10,
    },
    [paragraph(paragraphProps, children)],
  );

  return [label, box];
}

const rowTops = [80, 190, 300, 410];
const modes = ['head', 'middle', 'tail', 'clip'];
const rows = [];

for (let index = 0; index < modes.length; index += 1) {
  const mode = modes[index];
  const top = rowTops[index];

  rows.push(
    ...labelled(
      panelLeft,
      top,
      'ellipsizeMode: ' + mode + ', numberOfLines: 1',
      { color: white, fontSize: 16, numberOfLines: 1, ellipsizeMode: mode },
      sentence(),
    ),
  );
  rows.push(
    ...labelled(
      secondColumnLeft,
      top,
      'ellipsizeMode: ' + mode + ', numberOfLines: 2, letterSpacing: 1',
      { color: white, fontSize: 16, numberOfLines: 2, ellipsizeMode: mode, letterSpacing: 1 },
      sentence(),
    ),
  );
}

// A single token wider than its box is the one thing `clip` does that a line limit alone does not: there is no
// break opportunity to cut at, so the glyphs run past the frame unless the paint is clipped to it.
const unbreakable = labelled(
  panelLeft,
  520,
  'clip, one line, one token wider than the box',
  { color: white, fontSize: 16, numberOfLines: 1, ellipsizeMode: 'clip' },
  [rawText('Unbreakableantidisestablishmentarianismsupercalifragilistic')],
);

// A view-forming node inside a paragraph — here a nested <Paragraph> — is an inline attachment, and a paragraph
// carrying one is not searched: the line limit truncates it with no ellipsis, because a head or middle cut would
// drop placeholders from the front and every surviving one would then be paired with the wrong attachment.
const withAttachment = labelled(
  secondColumnLeft,
  520,
  'head, one line, with an inline attachment: the line limit only',
  { color: white, fontSize: 16, numberOfLines: 1, ellipsizeMode: 'head' },
  [
    rawText('Prose with '),
    paragraph({ color: amber, fontWeight: 'bold' }, [rawText('an inline attachment')]),
    rawText(' inside it that runs well past the end of the box it was given.'),
  ],
);

// One row per mode over a paragraph whose middle run is a nested <Text>: the searched cuts and the line limit
// have to land the same way they do on the unnested rows above, which is what #312 is.
const nestedRowTops = [620, 730];
const nestedRows = [];

for (let index = 0; index < modes.length; index += 1) {
  nestedRows.push(
    ...labelled(
      index % 2 === 0 ? panelLeft : secondColumnLeft,
      nestedRowTops[Math.floor(index / 2)],
      'ellipsizeMode: ' + modes[index] + ', numberOfLines: 1, nested Text',
      { color: white, fontSize: 16, numberOfLines: 1, ellipsizeMode: modes[index] },
      nestedSentence(),
    ),
  );
}

const heading = paragraph(
  { position: 'absolute', left: panelLeft, top: 32, width: 760, color: white, fontSize: 24, fontWeight: 'bold' },
  [rawText('ellipsizeMode: head, middle, tail and clip')],
);

const root = view({ flex: 1 }, [heading, ...rows, ...unbreakable, ...withAttachment, ...nestedRows]);
const rootChildren = fabric.createChildSet();

fabric.appendChildToSet(rootChildren, root);
fabric.completeRoot(surfaceId, rootChildren);

console.log('ellipsize: committed surface ' + surfaceId);
