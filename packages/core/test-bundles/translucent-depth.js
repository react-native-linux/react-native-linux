// Issue #105's perf criterion: a deep translucent tree's frame cost, as a number. Sixteen nested views at opacity
// 0.95, each beside a translucent bordered leaf, so the walk opens 32 opacity layers (#526 composites each
// translucent node once, through its own layer). The innermost box is moved every frame by the native driver.
// Sixteen of the layers, the nested views, enclose it, so its damage re-composites each of them every frame; the
// other sixteen, the sibling leaves, are painted again wherever that damage reaches them.

const surfaceId = 1;
const fabric = globalThis.nativeFabricUIManager;
const animated = globalThis.nativeModuleProxy.NativeAnimatedModule;

if (fabric === undefined || animated === null || animated === undefined) {
  throw new Error('translucent-depth needs nativeFabricUIManager and NativeAnimatedModule');
}

const depth = 16;
const retained = [];
let tag = 0;

const view = (props) => {
  tag += 2;
  retained.push({});

  return fabric.createNode(tag, 'View', surfaceId, props, retained[retained.length - 1]);
};

const moving = view({ width: 40, height: 40, backgroundColor: 0xffe5c07b | 0 });
const movingTag = tag;
let enclosing = moving;

for (let level = depth; level > 0; level -= 1) {
  const leaf = view({
    width: 30,
    height: 30,
    opacity: 0.8,
    borderWidth: 3,
    borderColor: 0xffffffff | 0,
    backgroundColor: 0xff98c379 | 0,
  });
  const nested = view({ padding: 6, flexDirection: 'row', opacity: 0.95, backgroundColor: 0x40306090 });

  fabric.appendChild(nested, leaf);
  fabric.appendChild(nested, enclosing);
  enclosing = nested;
}

const children = fabric.createChildSet();

fabric.appendChildToSet(children, enclosing);
fabric.completeRoot(surfaceId, children);

// An endless back-and-forth on translateX: what `Animated.loop` hands the native driver.
const ramp = Array.from({ length: 61 }, (unused, frame) => (frame <= 30 ? frame : 60 - frame) / 30);

animated.startOperationBatch();
animated.createAnimatedNode(1, { type: 'value', value: 0, offset: 0 });
animated.createAnimatedNode(2, { type: 'transform', transforms: [{ type: 'animated', property: 'translateX', nodeTag: 1 }] });
animated.createAnimatedNode(3, { type: 'style', style: { transform: 2 } });
animated.createAnimatedNode(4, { type: 'props', props: { style: 3 } });
animated.connectAnimatedNodes(1, 2);
animated.connectAnimatedNodes(2, 3);
animated.connectAnimatedNodes(3, 4);
animated.connectAnimatedNodeToView(4, movingTag);
animated.connectAnimatedNodeToShadowNodeFamily(4, moving);
animated.startAnimatingNode(1, 1, { type: 'frames', frames: ramp, toValue: 12, iterations: -1 }, () => {});
animated.finishOperationBatch();

console.log('translucent-depth: committed surface ' + surfaceId);
