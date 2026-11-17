import { describe, expect, it } from "vitest";
import { androidDependencyConfig } from "./android-dependency-config.ts";

const NOT_A_STRING = 1;
const LIBRARY_ROOT = "/libraries/react-native-safe-area-context";

const ANDROID = {
  cmakeListsPath: "src/main/jni/CMakeLists.txt",
  componentDescriptors: ["RNCSafeAreaProviderComponentDescriptor", "RNCSafeAreaViewComponentDescriptor"],
  libraryName: "safeareacontext",
};

const withAndroid = (android: unknown): unknown => ({
  dependency: { platforms: { android, macos: null, windows: null } },
});

const REFUSAL = `${LIBRARY_ROOT}/react-native.config.js: dependency.platforms.android must declare a cmakeListsPath`;

describe("androidDependencyConfig", () => {
  it("prints the Android CMakeLists, resolved under android/, and the declared component descriptors", () => {
    expect(androidDependencyConfig(LIBRARY_ROOT, withAndroid(ANDROID))).toStrictEqual({
      platforms: {
        android: {
          cmakeListsPath: `${LIBRARY_ROOT}/android/src/main/jni/CMakeLists.txt`,
          componentDescriptors: ANDROID.componentDescriptors,
        },
        linux: null,
      },
      root: LIBRARY_ROOT,
    });
  });

  it.each([
    ["a module that exports no object", null],
    ["a dependency without platforms", { dependency: {} }],
    ["a dependency without an Android platform", withAndroid(null)],
    ["an Android entry without a CMakeLists", withAndroid({ ...ANDROID, cmakeListsPath: null })],
    ["an Android entry without component descriptors", withAndroid({ ...ANDROID, componentDescriptors: [] })],
    ["component descriptors that are not strings", withAndroid({ ...ANDROID, componentDescriptors: [NOT_A_STRING] })],
  ])("refuses %s, naming the file", (_case, libraryConfig) => {
    expect(() => androidDependencyConfig(LIBRARY_ROOT, libraryConfig)).toThrow(REFUSAL);
  });
});
