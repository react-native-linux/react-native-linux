// The PlatformConstants proof for issue #23: the version a development bundle compares with its own, and the kernel
// release, through the TurboModule Platform.linux.ts reads.
//
// hello_react packages/core/test-bundles/platform-constants.js

const { isTesting, osVersion, reactNativeVersion } = globalThis.nativeModuleProxy.PlatformConstants.getConstants();
const { major, minor, patch, prerelease } = reactNativeVersion;

console.log(`platform-constants: reactNativeVersion ${major}.${minor}.${patch} prerelease=${prerelease}`);
console.log(`platform-constants: isTesting=${isTesting} osVersion non-empty=${osVersion.length > 0}`);
