import { describe, expect, it } from "vitest";
import path from "node:path";
import { resolveLinuxPackageVariantForResolvedFile } from "./package-variants.ts";

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
