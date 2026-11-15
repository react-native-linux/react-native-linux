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
