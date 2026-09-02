// The <Text> fixture for the golden-image rig. Every text capability the renderer claims gets one visually
// unambiguous element; docs/cpp-toolchain.md describes the picture this is expected to produce.
//
// Every string here is ASCII on purpose: it has to be covered by the vendored Noto Sans, because anything that
// falls through to fontconfig resolves to a different file on CI than on a development machine and stops being a
// golden. RTL and emoji are exactly that case and are deferred with the rest of #14's follow-ups.

const surfaceId = 1;
const fabric = globalThis.nativeFabricUIManager;

if (fabric === undefined) {
  throw new Error('nativeFabricUIManager was not installed');
}

// C++ holds instance handles weakly, so React retains them on its fibers. Keep them alive for the same reason.
const instanceHandles = [];

let nextTag = 2;

const createNode = (componentName, props, children = []) => {
  const instanceHandle = {};

  instanceHandles.push(instanceHandle);

  const node = fabric.createNode(nextTag, componentName, surfaceId, props, instanceHandle);

  nextTag += 1;

  for (const child of children) {
    fabric.appendChild(node, child);
  }

  return node;
};

const rawText = (text) => createNode('RawText', { text });

const text = (props, children) => createNode('Text', props, children);

const paragraph = (props, children) => createNode('Paragraph', props, children);

const view = (props, children = []) => createNode('View', props, children);

const white = 0xfff2f4f8 | 0;
const muted = 0xff9aa4b2 | 0;
const amber = 0xffe5c07b | 0;
const green = 0xff98c379 | 0;
const sky = 0xff61afef | 0;
const panel = 0xff1e2430 | 0;

const heading = paragraph(
  { position: 'absolute', left: 40, top: 32, width: 720, color: white, fontSize: 32, fontWeight: 'bold' },
  [rawText('Text renders on Linux')],
);

// 300 px wide, so the body has to wrap. The nested <Text> fragments prove that per-fragment attributes survive
// the flattening into one AttributedString: one is coloured, one is bold, one is italic.
const wrapping = view({ position: 'absolute', left: 40, top: 96, width: 300, backgroundColor: panel, padding: 12 }, [
  paragraph({ color: white, fontSize: 16, lineHeight: 24 }, [
    rawText('A paragraph that has to wrap inside a three hundred pixel column, mixing '),
    text({ color: amber, fontWeight: 'bold' }, [rawText('bold amber')]),
    rawText(', '),
    text({ color: green, fontStyle: 'italic' }, [rawText('italic green')]),
    rawText(' and plain fragments in one line box.'),
  ]),
]);

const truncated = view({ position: 'absolute', left: 380, top: 96, width: 380, backgroundColor: panel, padding: 12 }, [
  paragraph({ color: muted, fontSize: 16, numberOfLines: 1, ellipsizeMode: 'tail' }, [
    rawText('One line only, and this sentence is far too long to fit inside it, so it ends in an ellipsis.'),
  ]),
]);

const centered = paragraph(
  { position: 'absolute', left: 40, top: 300, width: 720, color: sky, fontSize: 20, textAlign: 'center' },
  [rawText('centered')],
);

const rightAligned = paragraph(
  { position: 'absolute', left: 40, top: 340, width: 720, color: sky, fontSize: 20, textAlign: 'right' },
  [rawText('right aligned')],
);

const spaced = paragraph(
  { position: 'absolute', left: 40, top: 396, width: 720, color: white, fontSize: 18, letterSpacing: 6 },
  [rawText('LETTER SPACED')],
);

const underlined = paragraph(
  {
    position: 'absolute',
    left: 40,
    top: 440,
    width: 720,
    color: amber,
    fontSize: 18,
    textDecorationLine: 'underline',
  },
  [rawText('underlined')],
);

const root = view({ flex: 1 }, [heading, wrapping, truncated, centered, rightAligned, spaced, underlined]);

const rootChildren = fabric.createChildSet();

fabric.appendChildToSet(rootChildren, root);
fabric.completeRoot(surfaceId, rootChildren);

console.log('text: committed surface ' + surfaceId);
