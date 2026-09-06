// The unmount-mid-gesture proof for issue #247. Two views stacked in normal flow: `box`, pressed first, and
// `survivor` underneath it. `box`'s own `topPointerDown` handler arms a timer, armed from the press it just
// observed rather than from the bundle loading, that commits a tree dropping `box` — a real asynchronous unmount
// landing on the JavaScript thread between the press and the release the injector delivers a frame apart, rather
// than a synchronous one inside the handler that would prove nothing about the two arriving on different turns.
// `survivor` reflows up into the row `box` left, so the release lands on a live node that never saw a
// `pointerDown` of its own.
//
// hello_react --inject-pointer packages/core/test-bundles/unmount-mid-press.js 150 90
//
// Every line carries a running click count, the same device `press-cancelled-by-scroll.js` uses: an ordered
// substring match can say a line was produced, but it cannot say a line never was, so the release line reading
// `clicks=0` is what proves `topClick` never fired rather than a gap in what the scenario happened to assert.
// `topBlur` on `box` arrives before the release is even processed, because `InputDispatcher::dispatch` re-syncs
// the focusable set — and therefore blurs a focused node the last commit dropped — before it looks at the frame's
// events at all.

const surfaceId = 1;
const fabric = globalThis.nativeFabricUIManager;

if (fabric === undefined) {
  throw new Error('nativeFabricUIManager was not installed');
}

// PointerEventsProcessor resolves an event target back to a shadow node through instanceHandle.stateNode.node,
// which is the shape React's fiber has. There is no React in this bundle, so the shape is built by hand, the same
// way pressable.js does.
const createInstanceHandle = () => ({ stateNode: { node: null } });

const rowProps = (backgroundColor) => ({
  width: 300,
  height: 100,
  backgroundColor: backgroundColor,
  accessible: true,
  onPointerEnter: true,
  onPointerLeave: true,
  onPointerMove: true,
  onPointerDown: true,
  onPointerUp: true,
  onClick: true,
  onFocus: true,
  onBlur: true,
});

const boxHandle = createInstanceHandle();
const box = fabric.createNode(10, 'View', surfaceId, rowProps(0xff3366cc | 0), boxHandle);

boxHandle.stateNode.node = box;

const survivorHandle = createInstanceHandle();
const survivor = fabric.createNode(11, 'View', surfaceId, rowProps(0xff98c379 | 0), survivorHandle);

survivorHandle.stateNode.node = survivor;

const container = fabric.createNode(2, 'View', surfaceId, { flex: 1 }, createInstanceHandle());

fabric.appendChild(container, box);
fabric.appendChild(container, survivor);

const firstChildren = fabric.createChildSet();

fabric.appendChildToSet(firstChildren, container);
fabric.completeRoot(surfaceId, firstChildren);

console.log('unmount-mid-press: committed surface ' + surfaceId);

let hasUnmountedBox = false;
let clickCount = 0;

// The single JavaScript entry point for every Fabric event. React's renderer installs its own dispatcher here;
// this one just reports and, on the press, drops `box` from the tree — so the C++ side of the pipeline is what
// the rest of the trace describes.
fabric.registerEventHandler((instanceHandle, type, payload) => {
  const name = instanceHandle === boxHandle ? 'box' : instanceHandle === survivorHandle ? 'survivor' : 'unknown';
  const hasPoint = payload !== null && payload !== undefined && payload.clientX !== undefined;
  const suffix = hasPoint ? ' at ' + payload.clientX + ',' + payload.clientY : '';

  if (type === 'topClick') {
    clickCount += 1;
  }

  console.log('unmount-mid-press: ' + type + ' on ' + name + suffix + ' clicks=' + clickCount);

  if (type === 'topPointerDown' && name === 'box' && !hasUnmountedBox) {
    hasUnmountedBox = true;

    // Armed from the press this handler just observed, not from the bundle loading — the delay is long enough
    // that the release the injector delivers a frame later reliably finds the commit already landed, and short
    // enough that the run's overall quiescence budget is nowhere near it.
    setTimeout(() => {
      const nextContainer = fabric.cloneNodeWithNewChildren(container);

      fabric.appendChild(nextContainer, survivor);

      const secondChildren = fabric.createChildSet();

      fabric.appendChildToSet(secondChildren, nextContainer);
      fabric.completeRoot(surfaceId, secondChildren);

      console.log('unmount-mid-press: unmounted box after press');
    }, 50);
  }
});
