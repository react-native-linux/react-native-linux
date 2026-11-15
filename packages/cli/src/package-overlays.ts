import { createRequire } from "node:module";
import path from "node:path";

const linuxPlatform = "linux";
const packageSubpathPattern = /.*node_modules[/\\](?<subpath>.+)\.(?:js|jsx|ts|tsx)$/u;

// Resolved as a dependency rather than as a sibling directory, so a packed copy finds it inside node_modules.
const coreDirectory = path.dirname(createRequire(import.meta.url).resolve("@react-native-linux/core/package.json"));

/**
 * ADR-0004: expo-modules-core installs its JavaScript `globalThis.expo`, the object model its web build and jest-expo
 * run on, only from the web variant of its polyfill, and its other modules read that global as they load. A linux
 * bundle takes this platform's polyfill instead, which installs upstream's and then answers `getViewConfig` (#662), so
 * Expo's own JavaScript runs unchanged over this platform's TurboModules and Fabric components.
 */
const linuxPackageOverlayIndex: Readonly<Record<string, string>> = {
  "expo-modules-core/build/polyfill/index": path.join(
    coreDirectory,
    "src-linux",
    "expo-modules-core",
    "polyfill",
    "index.linux.ts",
  ),
};

/**
 * ADR-0004's escape hatch: a resolved package file whose JavaScript has to differ on Linux, and the specifier of the
 * linux package a linux bundle resolves instead. `expo`'s `fetch`, which `Expo.fx` installs as the global one, runs
 * over the native `ExpoFetchModule`, whose request and response are native classes upstream's JavaScript extends,
 * the constructor-backed shape #663 has yet to prove; until #673, both keep React Native's own `fetch`.
 */
const linuxPackageSubstituteIndex: Readonly<Record<string, string>> = {
  "expo/build/winter/fetch/index": "@react-native-linux/expo/fetch",
};

const packageSubpathOf = (filePath: string): string => packageSubpathPattern.exec(filePath)?.groups?.["subpath"] ?? "";

/**
 * The file a linux bundle takes in place of a resolved package file that `linuxPackageOverlayIndex` names, the package
 * counterpart of `react-native`'s `linuxOverlayIndex`; null for every other file.
 */
const resolveLinuxPackageOverlayForResolvedFile = (filePath: string, platform: string | null): string | null =>
  platform === linuxPlatform ? (linuxPackageOverlayIndex[packageSubpathOf(filePath)] ?? null) : null;

/**
 * The specifier a linux bundle resolves instead of a resolved package file that `linuxPackageSubstituteIndex` names,
 * once the linux package that holds it is installed; null for every other file, as for a package alias whose linux
 * package is missing.
 */
const resolveLinuxPackageSubstituteForResolvedFile = (
  filePath: string,
  platform: string | null,
  isPackageResolvable: (packageName: string) => boolean,
): string | null => {
  const substitute = linuxPackageSubstituteIndex[packageSubpathOf(filePath)] ?? null;

  return platform !== linuxPlatform || substitute === null || !isPackageResolvable(path.posix.dirname(substitute))
    ? null
    : substitute;
};

export {
  coreDirectory,
  linuxPackageOverlayIndex,
  resolveLinuxPackageOverlayForResolvedFile,
  resolveLinuxPackageSubstituteForResolvedFile,
};
