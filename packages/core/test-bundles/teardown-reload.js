// The instance teardown gate of #76, in the real window: this bundle reloads itself until it has run as
// INSTANCE_COUNT instances, through DevSettings.reload as Fast Refresh's full reload does. Every instance paints
// an image, a paragraph and a view whose translateX a native-driven animation loops forever, so each teardown
// has glyphs, scene nodes and a running animation to give back. Instance 1 reloads once everything is drawn, so
// the first probe holds every one-time GPU allocation. Every later instance reloads as soon as it commits, racing
// a cold decode: the decoded-image cache is process-wide and keyed by URI, so each instance names the same asset
// through a different number of `./` segments.
//
// rnl_window --fabric packages/core/test-bundles/teardown-reload.js --frames 900
//
// At every reload the window prints an `[rnl-teardown]` line between the unloaded instance and the next one, and
// the e2e grader requires all of them to agree: the GPU resources Skia still holds once its cache is purged, and
// the live Wayland objects counted from WAYLAND_DEBUG. A first-to-second-instance difference is the leak, because
// a lifetime bug only shows up on the second instance. The instance number survives the reload in AsyncStorage,
// which the e2e driver keeps in a fresh XDG_DATA_HOME per run.

const INSTANCE_COUNT = 4;
const DRAWN_RELOAD_DELAY_MS = 300;
const COLD_DECODE_RELOAD_DELAY_MS = 0;
const IMAGE_ASSET = 'rnl-test-image.png';
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

const create = (viewName, props) => {
  const handle = {};

  handles.push(handle);
  nextTag += 1;

  return fabric.createNode(nextTag, viewName, surfaceId, props, handle);
};

const animate = (view, viewTag) => {
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
  animated.connectAnimatedNodeToView(propsTag, viewTag);
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
      source: { uri: './'.repeat(instance) + IMAGE_ASSET },
    }),
  );
  const view = create('View', {
    position: 'absolute',
    left: 280,
    top: 100,
    width: 160,
    height: 150,
    backgroundColor: 0xff3366cc | 0,
  });

  fabric.appendChildToSet(rootChildren, view);
  fabric.completeRoot(surfaceId, rootChildren);
  animate(view, nextTag);
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
      instance === 1 ? DRAWN_RELOAD_DELAY_MS : COLD_DECODE_RELOAD_DELAY_MS,
    );
  }
};

run().catch((error) => {
  console.error(`teardown: ${error.message}`);
});
