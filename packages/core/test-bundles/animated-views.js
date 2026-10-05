// The native-driver frame-cost scenarios for issue #124: core#50716's shape — views animating continuously through
// the native driver, the regression that shipped 20x slower — at N = 32, beside a 50-row ScrollView the
// `animated-views-scrolling` scenario wheels while every animation runs, which is core#34583's and core#38470's
// combination. The frame budget in each scenario's JSON is the gate; this bundle only has to keep the animations
// running and say so.
//
// There is no React and no Animated JavaScript in a bare bundle, so each view's graph is built through
// NativeAnimatedModule directly, as animated-scroll.js does: a value node, the transform, style and props nodes
// above it, and the connection to the view.

const surfaceId = 1;
const fabric = globalThis.nativeFabricUIManager;
const animated = globalThis.nativeModuleProxy.NativeAnimatedModule;

if (fabric === undefined || animated === null || animated === undefined) {
  throw new Error('animated-views needs nativeFabricUIManager and NativeAnimatedModule');
}

const animatedViewCount = 32;
const rowCount = 50;
const scrollViewTag = 3;

// A view at a frame, with a fiber-shaped handle retained here: C++ holds instance handles weakly, as React's fibers
// would otherwise hold them, and pointer and scroll events resolve their target through `stateNode.node`.
const handles = [];

const createView = (tag, componentName, [left, top, width, height], extraProps = {}) => {
  const handle = { stateNode: { node: null } };
  const props = { position: 'absolute', left, top, width, height, backgroundColor: 0xff1e2430 | 0, ...extraProps };

  handles.push(handle);
  handle.stateNode.node = fabric.createNode(tag, componentName, surfaceId, props, handle);

  return handle.stateNode.node;
};

const container = createView(2, 'View', [0, 0, 1280, 720]);
const scrollView = createView(scrollViewTag, 'ScrollView', [20, 20, 200, 400], { onScroll: true });
const content = createView(4, 'View', [0, 0, 200, rowCount * 40]);

for (let row = 0; row < rowCount; row += 1) {
  fabric.appendChild(content, createView(10 + row, 'View', [0, row * 40, 200, 36], { backgroundColor: 0xff98c379 | 0 }));
}

fabric.appendChild(scrollView, content);
fabric.appendChild(container, scrollView);

const animatedViews = [];

for (let index = 0; index < animatedViewCount; index += 1) {
  const view = createView(100 + index, 'View', [260 + (index % 8) * 44, 20 + Math.floor(index / 8) * 44, 32, 32], {
    backgroundColor: 0xff3366cc | 0,
  });

  fabric.appendChild(container, view);
  animatedViews.push(view);
}

const rootChildren = fabric.createChildSet();

fabric.appendChildToSet(rootChildren, container);
fabric.completeRoot(surfaceId, rootChildren);

// One second of travel and back at 60 frames a second, repeated forever: `iterations: -1` is how Animated.loop
// hands an endless animation to the native driver.
const frames = [];

for (let frame = 0; frame <= 60; frame += 1) {
  frames.push(frame <= 30 ? frame / 30 : (60 - frame) / 30);
}

// Every view's value is listened to, so the scenarios can say all of them moved — not just the first — and that
// all of them were still moving after the list started scrolling.
const movedTags = new Set();
const movedWhileScrollingTags = new Set();
let scrollEventCount = 0;

const recordMovement = (tags, line, tag) => {
  if (tags.size === animatedViewCount) {
    return;
  }

  tags.add(tag);

  if (tags.size === animatedViewCount) {
    console.log(line);
  }
};

globalThis.__rctDeviceEventEmitter = {
  emit: (eventName, event) => {
    if (eventName !== 'onAnimatedValueUpdate' || event.value <= 0) {
      return;
    }

    recordMovement(movedTags, 'animated-views: all ' + animatedViewCount + ' moving', event.tag);

    if (scrollEventCount > 0) {
      recordMovement(
        movedWhileScrollingTags,
        'animated-views: all ' + animatedViewCount + ' moving while scrolling',
        event.tag
      );
    }
  },
};

animated.startOperationBatch();

animatedViews.forEach((view, index) => {
  const valueTag = 1000 + index * 4;
  const transformTag = valueTag + 1;
  const styleTag = valueTag + 2;
  const propsTag = valueTag + 3;

  animated.createAnimatedNode(valueTag, { type: 'value', value: 0, offset: 0 });
  animated.createAnimatedNode(transformTag, {
    type: 'transform',
    transforms: [{ type: 'animated', property: 'translateX', nodeTag: valueTag }],
  });
  animated.createAnimatedNode(styleTag, { type: 'style', style: { transform: transformTag } });
  animated.createAnimatedNode(propsTag, { type: 'props', props: { style: styleTag } });
  animated.connectAnimatedNodes(valueTag, transformTag);
  animated.connectAnimatedNodes(transformTag, styleTag);
  animated.connectAnimatedNodes(styleTag, propsTag);
  animated.connectAnimatedNodeToView(propsTag, 100 + index);
  animated.connectAnimatedNodeToShadowNodeFamily(propsTag, view);
  animated.startAnimatingNode(index + 1, valueTag, { type: 'frames', frames, toValue: 8, iterations: -1 }, () => {});
});

animatedViews.forEach((view, index) => animated.startListeningToAnimatedNodeValue(1000 + index * 4));
animated.finishOperationBatch();

// Twelve scroll events is twelve frames the list moved in, which is what the p95 gate needs to see for the
// scrolling half of the run to be able to fail it: 12 of 239 intervals is past the 95th percentile.
const kScrollEventsMeasured = 12;

fabric.registerEventHandler((instanceHandle, type) => {
  if (type !== 'topScroll') {
    return;
  }

  scrollEventCount += 1;

  if (scrollEventCount === kScrollEventsMeasured) {
    console.log('animated-views: scrolled ' + kScrollEventsMeasured + ' times');
  }
});

console.log('animated-views: committed surface ' + surfaceId);
