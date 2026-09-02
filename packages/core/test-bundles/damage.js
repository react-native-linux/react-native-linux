// The damage-tracking fixture: one surface, two commits, three kinds of change. The first commit is the frame a
// window would already be showing; the second moves a view, recolours it, and unmounts another one. Everything the
// second commit does has to fall inside the damage the scene reports, which is what --damage-golden proves.

const surfaceId = 1;
const fabric = globalThis.nativeFabricUIManager;

if (fabric === undefined) {
  throw new Error('nativeFabricUIManager is not installed in this runtime');
}

// C++ holds instance handles weakly, so React retains them on its fibers. Keep them alive for the same reason.
const handles = [{}, {}, {}, {}];

const blue = 0xff3366cc | 0;
const green = 0xff98c379 | 0;
const amber = 0xffe5c07b | 0;
const red = 0xffe06c75 | 0;

// Never touched by the second commit: its pixels have to survive a redraw that only paints the damaged region.
const untouchedBox = fabric.createNode(
  2,
  'View',
  surfaceId,
  { position: 'absolute', left: 60, top: 50, width: 180, height: 130, backgroundColor: blue },
  handles[0],
);

// Moved and recoloured: the position it leaves and the position it arrives at both have to be damaged.
const movingBox = fabric.createNode(
  3,
  'View',
  surfaceId,
  { position: 'absolute', left: 300, top: 200, width: 140, height: 100, backgroundColor: green },
  handles[1],
);

// Unmounted by the second commit: only the position it leaves is damaged, and if it were not, it would still be
// on screen after a partial redraw.
const vanishingBox = fabric.createNode(
  4,
  'View',
  surfaceId,
  { position: 'absolute', left: 600, top: 80, width: 120, height: 120, backgroundColor: red },
  handles[2],
);

// Paints nothing, so Fabric flattens it away and the boxes reach the mounting layer parented to the root.
const container = fabric.createNode(5, 'View', surfaceId, { flex: 1 }, handles[3]);

fabric.appendChild(container, untouchedBox);
fabric.appendChild(container, movingBox);
fabric.appendChild(container, vanishingBox);

const firstChildren = fabric.createChildSet();

fabric.appendChildToSet(firstChildren, container);
fabric.completeRoot(surfaceId, firstChildren);

console.log('damage: committed the first frame of surface ' + surfaceId);

// A separate task, so this arrives as its own mounting transaction rather than as a second tree in the same
// commit. The delay is long enough that the host reliably observes the first frame before this one replaces it.
setTimeout(() => {
  const movedBox = fabric.cloneNodeWithNewProps(movingBox, {
    position: 'absolute',
    left: 420,
    top: 260,
    width: 140,
    height: 100,
    backgroundColor: amber,
  });
  const nextContainer = fabric.cloneNodeWithNewChildren(container);

  fabric.appendChild(nextContainer, untouchedBox);
  fabric.appendChild(nextContainer, movedBox);

  const secondChildren = fabric.createChildSet();

  fabric.appendChildToSet(secondChildren, nextContainer);
  fabric.completeRoot(surfaceId, secondChildren);

  console.log('damage: committed the second frame');
}, 1000);
