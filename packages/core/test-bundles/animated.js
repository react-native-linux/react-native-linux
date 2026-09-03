// The Animated proof for issues #127, #128 and #129: the C++ AnimatedModule is registered, accepts a whole
// operation batch through the generated NativeAnimatedModule spec, and — since the frame clock drives the shared
// animation backend — a frames animation actually runs and the animated value can be read back.
//
// hello_react --fabric packages/core/test-bundles/animated.js
//
// It needs --fabric because the module's JSI bindings run NativeAnimatedNodesManagerProvider::getOrCreate, which
// resolves the UIManager out of the runtime; without a Fabric host there is no UIManagerBinding to resolve.
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
const animationId = 1;
const toValue = 0.75;

// A single-entry `frames` ramp, so the value the animation lands on does not depend on how long a frame took: the
// driver's first step already runs off the end of the ramp and sets the node to `toValue` exactly. A longer ramp
// would interpolate against the wall-clock delta between two frames, which a headless run cannot pin down.
const framesConfig = { type: 'frames', frames: [1], toValue, iterations: 1 };

// The batch reaches the manager's UI-task queue, and the queue is drained by the animation frame the
// LinuxAnimationChoreographer of #129 delivers. So the read-back cannot ride in this batch: the driver has not
// stepped yet when the queue runs. It rides in the animation's end callback instead, which the frame that
// completes the animation posts back to JavaScript, and is answered by the frame after that.
animated.startOperationBatch();
animated.createAnimatedNode(valueTag, { type: 'value', value: 0, offset: 0 });
animated.createAnimatedNode(styleTag, { type: 'style', style: { opacity: valueTag } });
animated.connectAnimatedNodes(valueTag, styleTag);
animated.setAnimatedNodeValue(valueTag, 0.5);
animated.startAnimatingNode(animationId, valueTag, framesConfig, () => {
  animated.startOperationBatch();
  animated.getValue(valueTag, (value) => {
    console.log(`animated: value ${value}`);
  });
  animated.finishOperationBatch();
});
animated.finishOperationBatch();

console.log('animated: batch ok');
