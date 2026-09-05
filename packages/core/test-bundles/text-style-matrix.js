// The text-style matrix for the golden-image rig: `letterSpacing`, `textTransform`, `textDecorationLine` (with
// `textDecorationColor`/`textDecorationStyle`), `textShadow*` and `fontVariant`, each mapped from `TextAttributes`
// onto SkParagraph's `TextStyle` in `src/TextPipeline.cpp`, at two font sizes. Issue #250; docs/cpp-toolchain.md
// *Text* documents which of these were unmapped before this fixture and what each row proves.
//
// Every string is ASCII plus the Latin-1 Supplement letters `TextTransform.cpp`'s case mapping covers (à-ÿ,
// À-Þ), for the same reason `text.js` is ASCII: anything outside the vendored Noto Sans falls through to
// fontconfig and stops being reproducible.

const surfaceId = 1;
const fabric = globalThis.nativeFabricUIManager;

if (fabric === undefined) {
  throw new Error('nativeFabricUIManager was not installed');
}

// Fabric only holds instance handles weakly; retaining them here is what keeps React's fiber tree alive for as
// long as this bundle runs.
const retainedHandles = [];
let tagCounter = 1;

function allocateTag() {
  tagCounter += 1;

  return tagCounter;
}

function createNode(componentName, props, children) {
  const handle = {};

  retainedHandles.push(handle);

  const node = fabric.createNode(allocateTag(), componentName, surfaceId, props, handle);
  const childList = children === undefined ? [] : children;

  for (let index = 0; index < childList.length; index += 1) {
    fabric.appendChild(node, childList[index]);
  }

  return node;
}

function rawText(value) {
  return createNode('RawText', { text: value }, undefined);
}

function text(props, children) {
  return createNode('Text', props, children);
}

function paragraph(props, children) {
  return createNode('Paragraph', props, children);
}

function view(props, children) {
  return createNode('View', props, children);
}

const white = 0xfff2f4f8 | 0;
const muted = 0xff9aa4b2 | 0;
const amber = 0xffe5c07b | 0;
const green = 0xff98c379 | 0;
const sky = 0xff61afef | 0;

const smallFontSize = 16;
const largeFontSize = 28;
const rowLeft = 40;
const rowWidth = 900;
const labelHeight = 18;
const smallRowHeight = 30;
const largeRowHeight = 44;

// One property, at `smallFontSize` and then `largeFontSize`: the label names the row, and `contentAtSize` is a
// function of the font size so the same case (e.g. `letterSpacing` scaling with the type) can differ per size.
// Returns every node this row created, positioned starting at `top`, and how tall the whole row was.
const propertyRows = (top, caption, contentAtSize) => {
  const captionLabel = paragraph(
    { position: 'absolute', left: rowLeft, top, width: rowWidth, color: muted, fontSize: 12 },
    [rawText(caption)],
  );
  const smallTop = top + labelHeight;
  const smallRow = paragraph(
    { position: 'absolute', left: rowLeft, top: smallTop, width: rowWidth, fontSize: smallFontSize },
    contentAtSize(smallFontSize),
  );
  const largeTop = smallTop + smallRowHeight;
  const largeRow = paragraph(
    { position: 'absolute', left: rowLeft, top: largeTop, width: rowWidth, fontSize: largeFontSize },
    contentAtSize(largeFontSize),
  );

  return { nodes: [captionLabel, smallRow, largeRow], nextTop: largeTop + largeRowHeight };
};

let cursorTop = 24;

const heading = paragraph(
  {
    position: 'absolute',
    left: rowLeft,
    top: cursorTop,
    width: rowWidth,
    color: white,
    fontSize: 24,
    fontWeight: 'bold',
  },
  [rawText('Text style matrix: letterSpacing, textTransform, textDecorationLine, textShadow, fontVariant')],
);

cursorTop += 80;

const rows = [];

const addPropertyRows = (caption, contentAtSize) => {
  const result = propertyRows(cursorTop, caption, contentAtSize);

  rows.push(...result.nodes);
  cursorTop = result.nextTop;
};

addPropertyRows('letterSpacing 4', (fontSize) => [
  text({ color: white, letterSpacing: 4 }, [rawText('letter spacing at fontSize ' + fontSize)]),
]);

addPropertyRows(
  'textTransform: uppercase / lowercase / capitalize (a hyphen is not a word boundary)',
  () => [
    text({ color: amber, textTransform: 'uppercase' }, [rawText('shout ')]),
    text({ color: green, textTransform: 'lowercase' }, [rawText('QUIET ')]),
    text({ color: sky, textTransform: 'capitalize' }, [rawText('multi-word café value')]),
  ],
);

addPropertyRows('textDecorationLine: underline / line-through / both, with textDecorationColor', () => [
  text({ color: white, textDecorationLine: 'underline', textDecorationColor: amber }, [rawText('underlined ')]),
  text({ color: white, textDecorationLine: 'line-through', textDecorationColor: green }, [rawText('struck ')]),
  text({ color: white, textDecorationLine: 'underline line-through', textDecorationColor: sky }, [rawText('both')]),
]);

addPropertyRows('textDecorationStyle: solid / double / dotted / dashed / wavy', () => [
  text({ color: white, textDecorationLine: 'underline', textDecorationStyle: 'solid' }, [rawText('solid ')]),
  text({ color: white, textDecorationLine: 'underline', textDecorationStyle: 'double' }, [rawText('double ')]),
  text({ color: white, textDecorationLine: 'underline', textDecorationStyle: 'dotted' }, [rawText('dotted ')]),
  text({ color: white, textDecorationLine: 'underline', textDecorationStyle: 'dashed' }, [rawText('dashed ')]),
  text({ color: white, textDecorationLine: 'underline', textDecorationStyle: 'wavy' }, [rawText('wavy')]),
]);

addPropertyRows('textShadow: offset, radius and color', () => [
  text(
    { color: white, textShadowColor: amber, textShadowOffset: { width: 3, height: 3 }, textShadowRadius: 2 },
    [rawText('shadowed text')],
  ),
]);

addPropertyRows(
  'fontVariant: small-caps / tabular-nums / oldstyle-nums / lining-nums',
  () => [
    text({ color: white, fontVariant: ['small-caps'] }, [rawText('small caps ')]),
    text({ color: amber, fontVariant: ['tabular-nums'] }, [rawText('0123456789 ')]),
    text({ color: green, fontVariant: ['oldstyle-nums'] }, [rawText('0123456789 ')]),
    text({ color: sky, fontVariant: ['lining-nums'] }, [rawText('0123456789')]),
  ],
);

const styleMatrixRoot = view({ flex: 1 }, [heading, ...rows]);
const styleMatrixChildren = fabric.createChildSet();

fabric.appendChildToSet(styleMatrixChildren, styleMatrixRoot);
fabric.completeRoot(surfaceId, styleMatrixChildren);
console.log(`text-style-matrix: committed surface ${surfaceId}`);
