import {
  createLinuxResolveRequest,
  linuxOverlayIndex,
  linuxUpstreamVariantIndex,
  resolveLinuxOverlayForResolvedFile,
} from "./metro-config.ts";
import { describe, expect, it } from "vitest";
import path from "node:path";

const upstreamFile = (subpath: string): string =>
  path.join("/app", "node_modules", "react-native", "Libraries", ...subpath.split("/"));

const resolveWith = (
  resolution: unknown,
  moduleName: string,
): { readonly requested: string[]; readonly result: unknown } => {
  const requested: string[] = [];
  const context = {
    resolveRequest: (_context: unknown, name: string): unknown => {
      requested.push(name);

      return resolution;
    },
  };

  return { requested, result: createLinuxResolveRequest(() => false)(context, moduleName, "linux") };
};

describe("resolveLinuxOverlayForResolvedFile", () => {
  it("replaces an upstream file that has a linux overlay, whatever specifier reached it", () => {
    expect(resolveLinuxOverlayForResolvedFile(upstreamFile("Utilities/Platform.js"), "linux")).toBe(
      linuxOverlayIndex["Libraries/Utilities/Platform"],
    );
  });

  it.each([
    ["Network/RCTNetworking.js", "Network/RCTNetworking.android.js"],
    ["Utilities/BackHandler.js", "Utilities/BackHandler.ios.js"],
  ])("takes the named upstream variant of %s", (resolved, variant) => {
    expect(resolveLinuxOverlayForResolvedFile(upstreamFile(resolved), "linux")).toBe(upstreamFile(variant));
  });

  it.each([
    [upstreamFile("Components/View/View.js"), "linux"],
    [upstreamFile("Utilities/Platform.js"), "android"],
    ["/app/node_modules/react-native-svg/src/index.js", "linux"],
  ])("leaves %s alone on %s", (filePath, platform) => {
    expect(resolveLinuxOverlayForResolvedFile(filePath, platform)).toBeNull();
  });
});

describe("linuxUpstreamVariantIndex", () => {
  it("covers every self-re-exporting upstream file that has no overlay", () => {
    expect(Object.keys(linuxUpstreamVariantIndex)).toStrictEqual([
      "Libraries/Alert/RCTAlertManager",
      "Libraries/Components/AccessibilityInfo/legacySendAccessibilityEvent",
      "Libraries/Components/DrawerAndroid/DrawerLayoutAndroid",
      "Libraries/Components/ProgressBarAndroid/ProgressBarAndroid",
      "Libraries/Components/ToastAndroid/ToastAndroid",
      "Libraries/Image/Image",
      "Libraries/Network/RCTNetworking",
      "Libraries/Settings/Settings",
      "Libraries/StyleSheet/PlatformColorValueTypes",
      "Libraries/Utilities/BackHandler",
    ]);
  });
});

describe("createLinuxResolveRequest", () => {
  it("rewrites the specifier, resolves it, and substitutes the overlay for the file that produced", () => {
    const overlayPath = linuxOverlayIndex["Libraries/Utilities/Platform"];
    const { requested, result } = resolveWith(
      { filePath: upstreamFile("Utilities/Platform.js"), type: "sourceFile" },
      "react-native/Libraries/Utilities/Platform",
    );

    expect(requested).toStrictEqual([overlayPath]);
    expect(result).toStrictEqual({ filePath: overlayPath, type: "sourceFile" });
  });

  it.each([
    [{ filePath: upstreamFile("Components/View/View.js"), type: "sourceFile" }],
    [{ type: "empty" }],
    [{ filePath: 1, type: "sourceFile" }],
    [null],
  ])("returns %j unchanged when there is nothing to substitute", (resolution) => {
    expect(resolveWith(resolution, "./View").result).toBe(resolution);
  });
});
