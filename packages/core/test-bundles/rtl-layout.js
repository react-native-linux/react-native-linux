// The RTL layout fixture for issue #119: the same card twice, once under direction: 'ltr' and once under 'rtl', so
// each logical property shows which physical side it resolved to. Yoga and BaseViewProps resolve the directions
// upstream; this proves the renderer paints the geometry they hand it rather than assuming left-to-right.
//
// In each card: three swatches in a row (first is amber), the row inset by marginStart; a bar positioned with
// start: 0; a box with a thick borderStartWidth in red and a rounded borderTopStartRadius corner.

const surfaceId = 1;
const fabric = globalThis.nativeFabricUIManager;
const instanceHandles = [];
const panel = 0xff1e2430 | 0;
const amber = 0xffe5c07b | 0;
const sky = 0xff61afef | 0;
const green = 0xff98c379 | 0;
const red = 0xffe06c75 | 0;

let nextTag = 1;

const view = (props, children = []) => {
  const handle = {};

  instanceHandles.push(handle);
  nextTag += 1;

  const created = fabric.createNode(nextTag, 'View', surfaceId, props, handle);

  for (const child of children) {
    fabric.appendChild(created, child);
  }

  return created;
};

const swatch = (backgroundColor) => view({ backgroundColor, height: 40, marginEnd: 8, width: 40 });

const card = (top, direction) =>
  view({ backgroundColor: panel, direction, height: 220, left: 40, padding: 16, position: 'absolute', top, width: 360 }, [
    view({ flexDirection: 'row', marginStart: 40 }, [swatch(amber), swatch(sky), swatch(green)]),
    view({ backgroundColor: sky, height: 12, marginTop: 16, start: 0, width: 120 }),
    view({
      backgroundColor: green,
      borderColor: red,
      borderStartWidth: 12,
      borderTopStartRadius: 32,
      height: 80,
      marginTop: 16,
      width: 160,
    }),
  ]);

const rootChildren = fabric.createChildSet();

fabric.appendChildToSet(rootChildren, card(40, 'ltr'));
fabric.appendChildToSet(rootChildren, card(320, 'rtl'));
fabric.completeRoot(surfaceId, rootChildren);

console.log('rtl-layout: committed surface ' + surfaceId);
