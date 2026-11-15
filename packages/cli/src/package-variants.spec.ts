import { describe, expect, it } from "vitest";
import {
  resolveLinuxPackageSubstituteForResolvedFile,
  resolveLinuxPackageVariantForResolvedFile,
} from "./package-variants.ts";
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

describe("resolveLinuxPackageVariantForResolvedFile", () => {
  it("takes the web variant of expo-modules-core's polyfill, which installs globalThis.expo", () => {
    expect(resolveLinuxPackageVariantForResolvedFile(expoPolyfill("index.js"), "linux")).toBe(
      expoPolyfill("index.web.js"),
    );
  });

  it.each([
    [expoPolyfill("index.js"), "android"],
    [expoPolyfill("index.js"), null],
    [expoPolyfill("CoreModule.js"), "linux"],
    ["/app/src/polyfill/index.js", "linux"],
  ])("leaves %s alone on %s", (filePath, platform) => {
    expect(resolveLinuxPackageVariantForResolvedFile(filePath, platform)).toBeNull();
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
