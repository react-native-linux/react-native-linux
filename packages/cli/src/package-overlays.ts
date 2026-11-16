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
 *
 * react-native-keyboard-controller's native bindings take this platform's in their place (#279): a desktop has no soft
 * keyboard, so its keyboard height stays zero, its keyboard events never fire, a focused input layout synchronization
 * completes asynchronously, its views are Views, and `dismiss` blurs the focused input. The rest of the library,
 * T3 Code's patches to it included, runs as published. The overlay copies the export list of the library's private
 * `src/bindings.native.ts`, so a library upgrade that adds a binding would leave it undefined in a linux bundle until
 * something mounts it; the overlay's spec compares its exports with the library's `src/bindings.ts` to fail first.
 */
const linuxPackageOverlayIndex: Readonly<Record<string, string>> = {
  "expo-modules-core/build/polyfill/index": path.join(
    coreDirectory,
    "src-linux",
    "expo-modules-core",
    "polyfill",
    "index.linux.ts",
  ),
  "react-native-keyboard-controller/src/bindings.native": path.join(
    coreDirectory,
    "src-linux",
    "react-native-keyboard-controller",
    "bindings.linux.ts",
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
