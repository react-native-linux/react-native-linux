import { describe, expect, it } from "vitest";
import {
  linuxPackageOverlayIndex,
  resolveLinuxPackageOverlayForResolvedFile,
  resolveLinuxPackageSubstituteForResolvedFile,
} from "./package-overlays.ts";
import { existsSync } from "node:fs";
import path from "node:path";

const expoPolyfill = (fileName: string): string =>
  path.join(
    "/app",
    "node_modules",
    ".pnpm",
    "expo-modules-core@58.0.30",
    "node_modules",
    "expo-modules-core",
    "build",
    "polyfill",
    fileName,
  );

const expoFetch = (fileName: string): string =>
  path.join(
    "/app",
    "node_modules",
    ".pnpm",
    "expo@58.0.23",
    "node_modules",
    "expo",
    "build",
    "winter",
    "fetch",
    fileName,
  );

describe("resolveLinuxPackageOverlayForResolvedFile", () => {
  it("takes this platform's polyfill for expo-modules-core's, which installs globalThis.expo", () => {
    expect(resolveLinuxPackageOverlayForResolvedFile(expoPolyfill("index.js"), "linux")).toBe(
      linuxPackageOverlayIndex["expo-modules-core/build/polyfill/index"],
    );
  });

  it.each(Object.entries(linuxPackageOverlayIndex))("names an overlay for %s that exists", (_subpath, overlayPath) => {
    expect(existsSync(overlayPath)).toBe(true);
  });

  it.each([
    [expoPolyfill("index.js"), "android"],
    [expoPolyfill("index.js"), null],
    [expoPolyfill("CoreModule.js"), "linux"],
    ["/app/src/polyfill/index.js", "linux"],
  ])("leaves %s alone on %s", (filePath, platform) => {
    expect(resolveLinuxPackageOverlayForResolvedFile(filePath, platform)).toBeNull();
  });
});

const isLinuxExpoInstalled = (packageName: string): boolean => packageName === "@react-native-linux/expo";

describe("resolveLinuxPackageSubstituteForResolvedFile", () => {
  it("resolves expo's fetch as @react-native-linux/expo's, until the native ExpoFetchModule lands (#673)", () => {
    expect(resolveLinuxPackageSubstituteForResolvedFile(expoFetch("index.js"), "linux", isLinuxExpoInstalled)).toBe(
      "@react-native-linux/expo/fetch",
    );
  });

  it.each([
    [expoFetch("index.js"), "ios", isLinuxExpoInstalled],
    [expoFetch("index.js"), "linux", (): boolean => false],
    [expoFetch("fetch.js"), "linux", isLinuxExpoInstalled],
    [expoPolyfill("index.js"), "linux", isLinuxExpoInstalled],
  ])("leaves %s alone on %s, or without the linux package", (filePath, platform, isPackageResolvable) => {
    expect(resolveLinuxPackageSubstituteForResolvedFile(filePath, platform, isPackageResolvable)).toBeNull();
  });
});
