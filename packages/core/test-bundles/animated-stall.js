// Issue #19: a native-driven animation runs on the frame thread, not the JavaScript one. The bundle starts a
// half-second scale ramp and then holds the JavaScript thread in a busy loop for a full second. The frames that
// step the driver are the window loop's (LinuxAnimationChoreographer, #129), so by the time JavaScript runs again
// the ramp has already ended: the value it reads back is the end of the ramp, and the end callback reports a
// finished animation. A driver stepped by JavaScript would read back the start of the ramp instead. The scenario's
// frame budget is the other half of the proof: no frame hangs while JavaScript is held.
//
// hello_react --fabric packages/core/test-bundles/animated-stall.js

const turboModuleProxy = globalThis.__turboModuleProxy;
const animated =
  typeof turboModuleProxy === 'function'
    ? turboModuleProxy('NativeAnimatedModule')
    : globalThis.nativeModuleProxy.NativeAnimatedModule;

if (animated === null || animated === undefined) {
  throw new Error('the NativeAnimatedModule TurboModule was not registered');
}

const valueTag = 1;
const transformTag = 2;
const animationId = 1;
const stallMilliseconds = 1000;
// One entry per 60 Hz frame, so thirty entries are half a second: half the stall.
const rampFrameCount = 30;
const frames = Array.from({ length: rampFrameCount }, (_, index) => (index + 1) / rampFrameCount);

// The ramp scales: the native driver steps a transform node like any other, and only the value is read back.
const rampedTransform = { transforms: [{ nodeTag: valueTag, property: 'scale', type: 'animated' }], type: 'transform' };

animated.startOperationBatch();
animated.createAnimatedNode(valueTag, { offset: 0, type: 'value', value: 0 });
animated.createAnimatedNode(transformTag, rampedTransform);
animated.connectAnimatedNodes(valueTag, transformTag);
animated.startAnimatingNode(animationId, valueTag, { type: 'frames', frames, toValue: 1, iterations: 1 }, (result) => {
  console.log(`animated-stall: finished ${result.finished}`);
});
animated.finishOperationBatch();

console.log('animated-stall: started');

const stallStart = Date.now();

while (Date.now() - stallStart < stallMilliseconds) {
  // Hold the JavaScript thread: nothing on it runs until the loop ends.
}

animated.startOperationBatch();
animated.getValue(valueTag, (value) => {
  console.log(`animated-stall: value after the stall ${value}`);
});
animated.finishOperationBatch();
