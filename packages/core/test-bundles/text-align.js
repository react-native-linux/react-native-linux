// Issue #253: the six textAlign values in a left-to-right column and a right-to-left one. `start` and `end` resolve
// against each paragraph's own direction (#72), so in the Hebrew column `start` lands on the right where the Latin
// column puts it on the left; `left`, `right` and `center` do not move. Every line wraps once, so `justify` shows on
// the first line and leaves the last one at the start edge. Hebrew renders through the pinned Noto face (#545).

const surfaceId = 1;
const fabric = globalThis.nativeFabricUIManager;
const retained = [];
let tag = 2;

const create = (componentName, props) => {
  retained.push({});
  tag += 1;

  return fabric.createNode(tag, componentName, surfaceId, props, retained[retained.length - 1]);
};

const alignments = ['left', 'right', 'center', 'justify', 'start', 'end'];

const column = (left, writingDirection, prose) => {
  const container = create('View', { position: 'absolute', left, top: 16, width: 300 });

  alignments.forEach((textAlign) => {
    const paragraph = create('Paragraph', {
      color: 0xfff2f4f8 | 0,
      fontSize: 13,
      textAlign,
      writingDirection,
      marginBottom: 12,
      borderWidth: 1,
      borderColor: 0xff3e4451 | 0,
    });

    fabric.appendChild(paragraph, create('RawText', { text: textAlign + ': ' + prose }));
    fabric.appendChild(container, paragraph);
  });

  return container;
};

const surface = create('View', { flex: 1, backgroundColor: 0xff1e2430 | 0 });

fabric.appendChild(surface, column(32, 'ltr', 'a sentence long enough to wrap onto a second line here.'));
fabric.appendChild(surface, column(400, 'rtl', 'משפט ארוך מספיק כדי להישבר לשורה שנייה, ועוד כמה מילים כאן.'));

const children = fabric.createChildSet();

fabric.appendChildToSet(children, surface);
fabric.completeRoot(surfaceId, children);
