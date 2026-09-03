// The event-driven Animated proof for issue #131: a <ScrollView> whose contentOffset.y drives a sibling view's
// translateY through the Animated.event graph with useNativeDriver, and a per-frame trace that shows the scroll
// offset and the animated value agreeing inside one frame.
//
// hello_react --animated-scroll packages/core/test-bundles/animated-scroll.js 160 100 3
//
// Three wheel notches are 120 points of travel, so the last line of the trace is
// `animated-scroll: offset 120.00 value 120.00`, and every line before it carries the same number twice. A value
// that lagged its offset by a frame — the 1-frame latency React Native 0.86 fixed in the C++ backend — would put
// the previous frame's number on the right of every line.
//
// There is no React and no Animated JavaScript in a bare bundle, so the graph Animated.event would have built is
// built here through NativeAnimatedModule directly: a value node, the transform, style and props nodes above it,
// the connection to the view, and the mapping from topScroll's contentOffset.y onto the value.

const surfaceId = 1;
const fabric = globalThis.nativeFabricUIManager;

if (fabric === undefined) {
  throw new Error('nativeFabricUIManager was not installed');
}

// The host is bridgeless, so TurboModuleBinding installs `nativeModuleProxy` and nothing else.
const animated = globalThis.nativeModuleProxy.NativeAnimatedModule;

if (animated === null || animated === undefined) {
  throw new Error('the NativeAnimatedModule TurboModule was not registered');
}

// The shared AnimationBackend drops an animated mutation whose props node has no ShadowNodeFamily behind it, so
// this is what makes the trace mean anything: connectAnimatedNodeToView carries the tag and not the family.
if (typeof animated.connectAnimatedNodeToShadowNodeFamily !== 'function') {
  throw new Error('NativeAnimatedModule does not expose connectAnimatedNodeToShadowNodeFamily');
}

const scrollViewTag = 3;
const animatedBoxTag = 4;

// The event target resolves back to a shadow node through the instance handle and C++ holds it weakly, so the
// handles are retained here for the same reason React retains them on its fibers.
const handles = [];

const createNode = (tag, componentName, props) => {
  const handle = { stateNode: { node: null } };

  handle.stateNode.node = fabric.createNode(tag, componentName, surfaceId, props, handle);
  handles.push(handle);

  return handle.stateNode.node;
};

const container = createNode(2, 'View', { flex: 1 });

const scrollView = createNode(scrollViewTag, 'ScrollView', {
  position: 'absolute',
  left: 60,
  top: 60,
  width: 200,
  height: 150,
  backgroundColor: 0xff1e2430 | 0,
  onScroll: true,
});

// 470 points of content in a 150 point viewport, so 320 points of it can be scrolled.
const content = createNode(5, 'View', {
  position: 'absolute',
  left: 0,
  top: 0,
  width: 200,
  height: 470,
  backgroundColor: 0xff98c379 | 0,
});

// The scroll-linked view: outside the ScrollView, moved by nothing but the animated transform.
const animatedBox = createNode(animatedBoxTag, 'View', {
  position: 'absolute',
  left: 320,
  top: 60,
  width: 100,
  height: 100,
  backgroundColor: 0xff3366cc | 0,
});

fabric.appendChild(scrollView, content);
fabric.appendChild(container, scrollView);
fabric.appendChild(container, animatedBox);

const rootChildren = fabric.createChildSet();

fabric.appendChildToSet(rootChildren, container);
fabric.completeRoot(surfaceId, rootChildren);

const valueNodeTag = 100;
const transformNodeTag = 101;
const styleNodeTag = 102;
const propsNodeTag = 103;

// Node values arrive through the global TurboModule::emitDeviceEvent looks for. The manager sets the value inside
// the scroll event's own dispatch, on the frame thread, so this task is scheduled before the beat that carries
// the same scroll event to the handler below — which is why one line can carry both numbers.
let lastAnimatedValue = 0;

globalThis.__rctDeviceEventEmitter = {
  emit: (eventName, event) => {
    if (eventName === 'onAnimatedValueUpdate' && event.tag === valueNodeTag) {
      lastAnimatedValue = event.value;
    }
  },
};

animated.startOperationBatch();
animated.createAnimatedNode(valueNodeTag, { type: 'value', value: 0, offset: 0 });
animated.createAnimatedNode(transformNodeTag, {
  type: 'transform',
  transforms: [{ type: 'animated', property: 'translateY', nodeTag: valueNodeTag }],
});
animated.createAnimatedNode(styleNodeTag, { type: 'style', style: { transform: transformNodeTag } });
animated.createAnimatedNode(propsNodeTag, { type: 'props', props: { style: styleNodeTag } });
animated.connectAnimatedNodes(valueNodeTag, transformNodeTag);
animated.connectAnimatedNodes(transformNodeTag, styleNodeTag);
animated.connectAnimatedNodes(styleNodeTag, propsNodeTag);
animated.connectAnimatedNodeToView(propsNodeTag, animatedBoxTag);
animated.connectAnimatedNodeToShadowNodeFamily(propsNodeTag, animatedBox);
animated.startListeningToAnimatedNodeValue(valueNodeTag);
animated.addAnimatedEventToView(scrollViewTag, 'topScroll', {
  nativeEventPath: ['contentOffset', 'y'],
  animatedValueTag: valueNodeTag,
});
animated.finishOperationBatch();

fabric.registerEventHandler((instanceHandle, type, payload) => {
  if (type !== 'topScroll') {
    return;
  }

  console.log(
    'animated-scroll: offset ' + payload.contentOffset.y.toFixed(2) + ' value ' + lastAnimatedValue.toFixed(2)
  );
});

console.log('animated-scroll: committed surface ' + surfaceId);
