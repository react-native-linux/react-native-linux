// The Dimensions proof for issue #50, and the first bundle that talks to a TurboModule rather than to
// nativeFabricUIManager.
//
// hello_react --fabric packages/core/test-bundles/dimensions.js
// hello_react --resize packages/core/test-bundles/dimensions.js 640 480
//
// The first prints the constants the surface booted with. The second prints those and then the one
// didUpdateDimensions the resize is allowed to emit, which is the coalescing contract of rn-macos#2083.
//
// There is no React and no Dimensions module in a bare bundle, so this is what those two would do: read the
// module the way TurboModuleRegistry does, and answer the emitter the way RCTDeviceEventEmitter does.

const turboModuleProxy = globalThis.__turboModuleProxy;
const deviceInfo =
  typeof turboModuleProxy === 'function' ? turboModuleProxy('DeviceInfo') : globalThis.nativeModuleProxy.DeviceInfo;

if (deviceInfo === null || deviceInfo === undefined) {
  throw new Error('the DeviceInfo TurboModule was not registered');
}

const describe = (metrics) =>
  `${metrics.width}x${metrics.height} scale ${metrics.scale} fontScale ${metrics.fontScale}`;

const report = (label, payload) => {
  console.log(`dimensions: ${label} window ${describe(payload.window)}`);
  console.log(`dimensions: ${label} screen ${describe(payload.screen)}`);
};

globalThis.__rctDeviceEventEmitter = {
  emit: (eventName, payload) => {
    report(eventName, payload);
  },
};

report('constants', deviceInfo.getConstants().Dimensions);
