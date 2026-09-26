// The degenerate-value fixture for the golden-image rig, issue #73: a NaN, an infinity or an empty size in a style
// must be a picture, not a crash and not a poisoned rectangle. The first box on the row is the reference, and
// each of the next three carries one non-finite value: the transform's NaN reaches the scene and is refused
// there, while the opacity's NaN and the radius's infinity are folded to their defaults before it.
//
// hello_react --golden packages/core/test-bundles/non-finite.js /tmp/rnl-non-finite.png
//
// Expected: boxes two to four paint exactly like the reference, and the last two — a zero width and a negative
// height — paint nothing at all.

const surfaceId = 1;
const fabric = globalThis.nativeFabricUIManager;

if (fabric === undefined) {
  throw new Error('nativeFabricUIManager was not installed');
}

const box = (tag, left, extraProps) =>
  fabric.createNode(
    tag,
    'View',
    surfaceId,
    {
      position: 'absolute',
      left,
      top: 40,
      width: 60,
      height: 60,
      backgroundColor: 0xff3366cc | 0,
      ...extraProps,
    },
    {},
  );

const container = fabric.createNode(10, 'View', surfaceId, { flex: 1, backgroundColor: 0xfff4f4f4 | 0 }, {});

fabric.appendChild(container, box(11, 20, {}));
fabric.appendChild(container, box(12, 100, { transform: [{ scale: NaN }] }));
fabric.appendChild(container, box(13, 180, { opacity: NaN }));
fabric.appendChild(container, box(14, 260, { borderRadius: Infinity }));
fabric.appendChild(container, box(15, 340, { width: 0 }));
fabric.appendChild(container, box(16, 420, { height: -10 }));

const rootChildren = fabric.createChildSet();

fabric.appendChildToSet(rootChildren, container);
fabric.completeRoot(surfaceId, rootChildren);

console.log('non-finite: committed surface ' + surfaceId);
