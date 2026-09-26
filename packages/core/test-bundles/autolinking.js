// The autolinking proof for issue #147: a C++ TurboModule from an unmodified create-react-native-library
// `cpp-library`, discovered by scripts/autolink.ts, built through its own android/CMakeLists.txt, registered by the
// generated rnl_autolinking.cpp, and called from JavaScript the way TurboModuleRegistry.getEnforcing would.
//
// node scripts/autolink.ts <react-native config JSON> build/autolinking
// cmake --preset dev -DRNL_AUTOLINKING_CMAKE=build/autolinking/rnl_autolinking.cmake
// hello_react packages/core/test-bundles/autolinking.js

const turboModuleProxy = globalThis.__turboModuleProxy;
const cppLibrary =
  typeof turboModuleProxy === 'function' ? turboModuleProxy('CppLibrary') : globalThis.nativeModuleProxy.CppLibrary;

console.log(`autolinking: CppLibrary.multiply(3, 4) = ${cppLibrary.multiply(3, 4)}`);
