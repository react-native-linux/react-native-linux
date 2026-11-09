// The instance teardown gate of #76, in the real window: this bundle reloads itself until it has run as
// INSTANCE_COUNT instances, through DevSettings.reload as Fast Refresh's full reload does. Every instance paints
// an image, a paragraph and a view whose translateX a native-driven animation loops forever, so each teardown
// has glyphs, scene nodes and a running animation to give back. Odd instances reload once their image has
// decoded and leave a texture behind them; even ones reload as soon as they commit, with the decode in flight.
//
// rnl_window --fabric packages/core/test-bundles/teardown-reload.js --frames 900
//
// At every reload the window prints an `[rnl-teardown]` line between the unloaded instance and the next one, and
// the e2e grader requires all of them to agree: the GPU resources Skia still holds once its cache is purged, and
// the live Wayland objects counted from WAYLAND_DEBUG. A first-to-second-instance difference is the leak, because
// a lifetime bug only shows up on the second instance. The instance number survives the reload in AsyncStorage,
// which the e2e driver keeps in a fresh XDG_DATA_HOME per run.

const INSTANCE_COUNT = 4;
const DECODED_RELOAD_DELAY_MS = 300;
const IN_FLIGHT_RELOAD_DELAY_MS = 0;
const ANIMATED_VIEW_TAG = 100;
const animationFrames = [0, 0.5, 1, 0.5, 0];
const surfaceId = 1;
const fabric = globalThis.nativeFabricUIManager;
const turboModuleProxy = globalThis.__turboModuleProxy;

if (fabric === undefined) {
  throw new Error('nativeFabricUIManager was not installed');
}

const nativeModule = (name) =>
  typeof turboModuleProxy === 'function' ? turboModuleProxy(name) : globalThis.nativeModuleProxy[name];
const storage = nativeModule('RNAsyncStorage');
const devSettings = nativeModule('DevSettings');
const animated = nativeModule('NativeAnimatedModule');

// C++ holds instance handles weakly, so React retains them on its fibers. Keep them alive for the same reason.
const handles = [];
let nextTag = 2;

const create = (viewName, props, tag = nextTag + 1) => {
  const handle = {};

  handles.push(handle);
  nextTag = Math.max(nextTag, tag);

  return fabric.createNode(tag, viewName, surfaceId, props, handle);
};

const animate = (view) => {
  const [valueTag, transformTag, styleTag, propsTag] = [1000, 1001, 1002, 1003];

  animated.startOperationBatch();
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
  animated.connectAnimatedNodeToView(propsTag, ANIMATED_VIEW_TAG);
  animated.connectAnimatedNodeToShadowNodeFamily(propsTag, view);
  animated.startAnimatingNode(1, valueTag, { type: 'frames', frames: animationFrames, toValue: 40, iterations: -1 }, () => {});
  animated.finishOperationBatch();
};

const render = (instance) => {
  const paragraph = create('Paragraph', { position: 'absolute', left: 40, top: 40, width: 400, fontSize: 24 });
  const rootChildren = fabric.createChildSet();

  fabric.appendChild(paragraph, create('RawText', { text: `teardown instance ${instance}` }));
  fabric.appendChildToSet(rootChildren, paragraph);
  fabric.appendChildToSet(
    rootChildren,
    create('Image', {
      position: 'absolute',
      left: 40,
      top: 100,
      width: 200,
      height: 150,
      resizeMode: 'cover',
      source: { uri: 'rnl-test-image.png' },
    }),
  );
  const view = create(
    'View',
    { position: 'absolute', left: 280, top: 100, width: 160, height: 150, backgroundColor: 0xff3366cc | 0 },
    ANIMATED_VIEW_TAG,
  );

  fabric.appendChildToSet(rootChildren, view);
  fabric.completeRoot(surfaceId, rootChildren);
  animate(view);
};

const run = async () => {
  const [entry] = await storage.getValues('teardown', ['instance']);
  const instance = Number(entry?.value ?? '0') + 1;

  await storage.setValues('teardown', [{ key: 'instance', value: String(instance) }]);
  render(instance);
  console.log(`teardown: instance ${instance} committed`);

  if (instance < INSTANCE_COUNT) {
    setTimeout(
      () => devSettings.reload(),
      instance % 2 === 1 ? DECODED_RELOAD_DELAY_MS : IN_FLIGHT_RELOAD_DELAY_MS,
    );
  }
};

run().catch((error) => {
  console.error(`teardown: ${error.message}`);
});
