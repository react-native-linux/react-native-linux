// The Animated proof for issues #127 and #128: the C++ AnimatedModule is registered, reachable by name, and
// accepts a whole operation batch through the generated NativeAnimatedModule spec.
//
// hello_react --fabric packages/core/test-bundles/animated.js
//
// It needs --fabric because the module's JSI bindings run NativeAnimatedNodesManagerProvider::getOrCreate, which
// resolves the UIManager out of the runtime; without a Fabric host there is no UIManagerBinding to resolve.
//
// The operations below are queued, not executed: NativeAnimatedNodesManager runs its UI-task queue from
// onRender, and nothing calls that until the animation choreographer of #129 delivers frames. So `getValue` is
// wired up here and its callback deliberately not asserted on — what this bundle proves is registration and the
// full argument path through the codegen spec, which is the whole of what #127 and #128 own.
//
// There is no React and no Animated JavaScript in a bare bundle, so this reads the module the way React Native's
// TurboModuleRegistry does.

const turboModuleProxy = globalThis.__turboModuleProxy;
const animated =
  typeof turboModuleProxy === 'function'
    ? turboModuleProxy('NativeAnimatedModule')
    : globalThis.nativeModuleProxy.NativeAnimatedModule;

if (animated === null || animated === undefined) {
  throw new Error('the NativeAnimatedModule TurboModule was not registered');
}

console.log('animated: module present');

const valueTag = 1;
const styleTag = 2;

animated.startOperationBatch();
animated.createAnimatedNode(valueTag, { type: 'value', value: 0, offset: 0 });
animated.createAnimatedNode(styleTag, { type: 'style', style: { opacity: valueTag } });
animated.connectAnimatedNodes(valueTag, styleTag);
animated.setAnimatedNodeValue(valueTag, 0.5);
animated.getValue(valueTag, (value) => {
  console.log(`animated: value ${value}`);
});
animated.finishOperationBatch();

console.log('animated: batch ok');
