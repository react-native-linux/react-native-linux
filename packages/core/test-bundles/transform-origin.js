// The transform-origin fixture for issue #104. Each cell is a 100x60 box turned 45 degrees about a different
// transformOrigin, over a grey outline of the unturned frame, so the pivot is the corner the two share. The last
// cell is perspective(200) rotateY(30deg), which the renderer reduces to its 2D affine part: it shows the reduction,
// not the projection a 3D renderer would draw (docs/cpp-toolchain.md records the error in pixels).

const surfaceId = 1;
const fabric = globalThis.nativeFabricUIManager;

// C++ holds instance handles weakly, so React retains them on its fibers. Keep them alive for the same reason.
const instanceHandles = [];
const outline = 0xff6b7280 | 0;
const fill = 0xcc3366cc | 0;

// Each cell is two nodes, the unturned outline and the turned box inside it, so the tags go up in pairs.
const cell = (index, left, top, transformProps) => {
  const outlineHandle = {};
  const boxHandle = {};
  const frame = { position: 'absolute', width: 100, height: 60 };
  const cellNode = fabric.createNode(2 + index * 2, 'View', surfaceId, { ...frame, left, top, borderWidth: 1, borderColor: outline }, outlineHandle);
  const boxNode = fabric.createNode(3 + index * 2, 'View', surfaceId, { ...frame, left: -1, top: -1, backgroundColor: fill, ...transformProps }, boxHandle);

  instanceHandles.push(outlineHandle, boxHandle);
  fabric.appendChild(cellNode, boxNode);

  return cellNode;
};

const turn = [{ rotate: '45deg' }];
const cells = [
  cell(0, 80, 80, { transform: turn, transformOrigin: [0, 0, 0] }),
  cell(1, 330, 80, { transform: turn, transformOrigin: ['100%', 0, 0] }),
  cell(2, 580, 80, { transform: turn }),
  cell(3, 80, 330, { transform: turn, transformOrigin: [0, '100%', 0] }),
  cell(4, 330, 330, { transform: turn, transformOrigin: ['100%', '100%', 0] }),
  cell(5, 580, 330, { transform: [{ perspective: 200 }, { rotateY: '30deg' }] }),
];

const rootChildren = fabric.createChildSet();

for (const cellNode of cells) {
  fabric.appendChildToSet(rootChildren, cellNode);
}
fabric.completeRoot(surfaceId, rootChildren);

console.log('transform-origin: committed surface ' + surfaceId);
