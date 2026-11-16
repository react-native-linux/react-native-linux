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

const packageSubpathOf = (filePath: string): string => packageSubpathPattern.exec(filePath)?.groups?.["subpath"] ?? "";

/**
 * The file a linux bundle takes in place of a resolved package file that `linuxPackageOverlayIndex` names, the package
 * counterpart of `react-native`'s `linuxOverlayIndex`; null for every other file.
 */
const resolveLinuxPackageOverlayForResolvedFile = (filePath: string, platform: string | null): string | null =>
  platform === linuxPlatform ? (linuxPackageOverlayIndex[packageSubpathOf(filePath)] ?? null) : null;

export { coreDirectory, linuxPackageOverlayIndex, resolveLinuxPackageOverlayForResolvedFile };
