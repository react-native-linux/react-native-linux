import path from "node:path";

const EMPTY_LENGTH = 0;

interface AndroidDependencyConfig {
  readonly root: string;
  readonly platforms: {
    readonly linux: null;
    readonly android: { readonly cmakeListsPath: string; readonly componentDescriptors: readonly string[] };
  };
}

const isRecord = (value: unknown): value is Record<string, unknown> =>
  typeof value === "object" && value !== null && !Array.isArray(value);

const isNonEmptyStringArray = (value: unknown): value is readonly string[] =>
  Array.isArray(value) && value.length > EMPTY_LENGTH && value.every((entry) => typeof entry === "string");

/**
 * The `react-native config` entry for a library whose Linux build is its Android one (#150): the CMakeLists and the
 * component descriptors its react-native.config.js declares under `android`, as `react-native config` prints them,
 * with the CMakeLists path resolved against the library's `android` directory. `libraryConfig` is what that file
 * exports; anything else is refused with the file and the field named.
 */
const androidDependencyConfig = (libraryRoot: string, libraryConfig: unknown): AndroidDependencyConfig => {
  const dependency = isRecord(libraryConfig) ? libraryConfig["dependency"] : null;
  const platforms = isRecord(dependency) ? dependency["platforms"] : null;
  const android = isRecord(platforms) ? platforms["android"] : null;
  const cmakeListsPath = isRecord(android) ? android["cmakeListsPath"] : null;
  const componentDescriptors = isRecord(android) ? android["componentDescriptors"] : null;

  if (typeof cmakeListsPath !== "string" || !isNonEmptyStringArray(componentDescriptors)) {
    throw new Error(
      `${path.join(libraryRoot, "react-native.config.js")}: dependency.platforms.android must declare a cmakeListsPath and a non-empty componentDescriptors`,
    );
  }

  return {
    platforms: {
      android: { cmakeListsPath: path.resolve(libraryRoot, "android", cmakeListsPath), componentDescriptors },
      linux: null,
    },
    root: libraryRoot,
  };
};

export { androidDependencyConfig };
