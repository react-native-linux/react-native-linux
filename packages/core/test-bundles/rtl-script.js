// Issue #72's RTL-script golden: Arabic and Hebrew paragraphs shaped through the pinned Noto Sans Arabic and Noto
// Sans Hebrew faces, so the picture is the same on every machine. Each script appears right-aligned under its
// natural right-to-left direction, inside a narrow box that makes it wrap, and once mixed with Latin and digits, the
// bidi run order SkParagraph resolves.

const surfaceId = 1;
const fabric = globalThis.nativeFabricUIManager;
const kept = [];
let tag = 2;

const create = (componentName, props) => {
  kept.push({});
  tag += 1;

  return fabric.createNode(tag, componentName, surfaceId, props, kept[kept.length - 1]);
};

const line = (text, style) => {
  const paragraph = create('Paragraph', { color: 0xfff2f4f8 | 0, fontSize: 22, marginBottom: 14, ...style });

  fabric.appendChild(paragraph, create('RawText', { text }));

  return paragraph;
};

const column = create('View', { flex: 1, padding: 24, backgroundColor: 0xff1e2430 | 0 });

[
  line('مرحبا بالعالم، هذا نص عربي يلتف داخل مربع ضيق.', { writingDirection: 'rtl', width: 360 }),
  line('שלום עולם, זהו טקסט עברי שנשבר בתוך תיבה צרה.', { writingDirection: 'rtl', width: 360 }),
  line('React Native 0.87 يعمل على Linux منذ 2026.', { writingDirection: 'rtl' }),
  line('Hebrew inside Latin: גרסה 3.1 עובדת היטב today.', { writingDirection: 'ltr' }),
].forEach((paragraph) => fabric.appendChild(column, paragraph));

const children = fabric.createChildSet();

fabric.appendChildToSet(children, column);
fabric.completeRoot(surfaceId, children);
