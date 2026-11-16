import { describe, expect, it } from "vitest";
import { linuxPackageOverlayIndex, resolveLinuxPackageOverlayForResolvedFile } from "./package-overlays.ts";
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

const keyboardControllerSource = (fileName: string): string =>
  path.join(
    "/app",
    "node_modules",
    ".pnpm",
    "react-native-keyboard-controller@1.23.0_patch_hash=1c7c80317ef0",
    "node_modules",
    "react-native-keyboard-controller",
    "src",
    fileName,
  );

describe("resolveLinuxPackageOverlayForResolvedFile", () => {
  it("takes this platform's polyfill for expo-modules-core's, which installs globalThis.expo", () => {
    expect(resolveLinuxPackageOverlayForResolvedFile(expoPolyfill("index.js"), "linux")).toBe(
      linuxPackageOverlayIndex["expo-modules-core/build/polyfill/index"],
    );
  });

  it("takes this platform's bindings for react-native-keyboard-controller's native ones, which report no keyboard", () => {
    expect(resolveLinuxPackageOverlayForResolvedFile(keyboardControllerSource("bindings.native.ts"), "linux")).toBe(
      linuxPackageOverlayIndex["react-native-keyboard-controller/src/bindings.native"],
    );
  });

  it.each(Object.entries(linuxPackageOverlayIndex))("names an overlay for %s that exists", (_subpath, overlayPath) => {
    expect(existsSync(overlayPath)).toBe(true);
  });

  it.each([
    [expoPolyfill("index.js"), "android"],
    [expoPolyfill("index.js"), null],
    [expoPolyfill("CoreModule.js"), "linux"],
    [keyboardControllerSource("bindings.native.ts"), "android"],
    [keyboardControllerSource("module.ts"), "linux"],
    ["/app/src/polyfill/index.js", "linux"],
  ])("leaves %s alone on %s", (filePath, platform) => {
    expect(resolveLinuxPackageOverlayForResolvedFile(filePath, platform)).toBeNull();
  });
});
