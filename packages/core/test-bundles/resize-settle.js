// The e2e half of issue #42: text at the sizes that broke macOS's measure cache — 6 points, wrapped and
// ellipsised in boxes narrower than it — three paragraphs of a fixed width and three at half the window, which the
// `resize-settle` scenario drags through a sequence of configures and then leaves alone. The frame journal is the
// assertion: one painted frame per configure, and none once the drag stops.

const surfaceId = 1;
const fabric = globalThis.nativeFabricUIManager;

if (fabric === undefined) {
  throw new Error('nativeFabricUIManager was not installed');
}

// Fabric holds instance handles weakly, so this array keeps them alive for as long as the bundle runs.
const handles = [];
let nextTag = 2;

const create = (componentName, props) => {
  handles.push({});
  nextTag += 1;

  return fabric.createNode(nextTag, componentName, surfaceId, props, handles[handles.length - 1]);
};

const prose = 'One line of prose long enough that no box on this surface can hold all of it at once, or twice.';
const container = create('View', { flex: 1, backgroundColor: 0xff1e2430 | 0 });

[
  { width: 120, numberOfLines: 1, ellipsizeMode: 'tail' },
  { width: 60, numberOfLines: 3 },
  { width: 40, numberOfLines: 1, ellipsizeMode: 'middle' },
  { width: '50%', numberOfLines: 1, ellipsizeMode: 'tail' },
  { width: '50%', numberOfLines: 2 },
  { width: '50%' },
].forEach((layout) => {
  const paragraph = create('Paragraph', { color: 0xfff2f4f8 | 0, fontSize: 6, ...layout });

  fabric.appendChild(paragraph, create('RawText', { text: prose }));
  fabric.appendChild(container, paragraph);
});

const rootChildren = fabric.createChildSet();

fabric.appendChildToSet(rootChildren, container);
fabric.completeRoot(surfaceId, rootChildren);

console.log('resize-settle: committed surface ' + surfaceId);
