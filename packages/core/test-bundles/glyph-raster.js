// The glyph rasterization fixture for the golden-image rig, issue #71: thin glyphs over an opaque region and
// over a transparent one, in both light-on-dark and dark-on-light. The golden pins the grayscale-coverage
// policy - a subpixel LCD pass would fringe the stems with colour, and the golden has zero tolerance.
//
// hello_react --golden packages/core/test-bundles/glyph-raster.js /tmp/rnl-glyph-raster.png

const surfaceId = 1;
const fabric = globalThis.nativeFabricUIManager;

if (fabric === undefined) {
  throw new Error('nativeFabricUIManager was not installed');
}

// C++ holds instance handles weakly, so the bundle retains one per node, pre-shaped the way the event target
// resolution reads them back.
const handles = [];

const createNode = (tag, componentName, props, children = []) => {
  const handle = { stateNode: { node: null } };
  const node = fabric.createNode(tag, componentName, surfaceId, props, handle);

  handle.stateNode.node = node;
  handles.push(handle);

  for (const child of children) {
    fabric.appendChild(node, child);
  }

  return node;
};

const paragraph = (tag, props, string) =>
  createNode(tag, 'Paragraph', props, [createNode(tag + 100, 'RawText', { text: string })]);

const panel = 0xff1e2430 | 0;
const white = 0xffffffff | 0;
const black = 0xff000000 | 0;

const opaquePanel = createNode(10, 'View', {
  position: 'absolute',
  left: 40,
  top: 40,
  width: 400,
  height: 160,
  backgroundColor: panel,
});

// Thin light-on-dark over the opaque panel: hairline strokes are where LCD fringing shows.
const thinLightOnDark = paragraph(
  11,
  { position: 'absolute', left: 60, top: 60, width: 360, color: white, fontSize: 12 },
  'thin light on dark: ili | 1 | lli | HHH 111 lll',
);

const secondLightOnDark = paragraph(
  13,
  { position: 'absolute', left: 60, top: 120, width: 360, color: white, fontSize: 12 },
  'thin dark on light: --- === === lll 111',
);

// Nothing behind the next pair: the glyphs sit on the surface's transparent regions.
const thinDarkOnTransparent = paragraph(
  14,
  { position: 'absolute', left: 60, top: 260, width: 360, color: black, fontSize: 12 },
  'thin dark on transparent: iii | 1 | ill',
);

const thinColourOnTransparent = paragraph(
  15,
  { position: 'absolute', left: 60, top: 320, width: 360, color: 0xff3366cc | 0, fontSize: 12 },
  'thin colour on transparent: HHH 111',
);

const container = createNode(12, 'View', { flex: 1 }, [
  opaquePanel,
  thinLightOnDark,
  secondLightOnDark,
  thinDarkOnTransparent,
  thinColourOnTransparent,
]);

const rootChildren = fabric.createChildSet();

fabric.appendChildToSet(rootChildren, container);
fabric.completeRoot(surfaceId, rootChildren);

console.log('glyph-raster: committed surface ' + surfaceId);
