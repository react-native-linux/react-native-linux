import {
  createLinuxResolveRequest,
  linuxOverlayIndex,
  linuxUpstreamVariantIndex,
  resolveLinuxOverlayForResolvedFile,
} from "./metro-config.ts";
import { describe, expect, it } from "vitest";
import { linuxPackageOverlayIndex } from "./package-overlays.ts";
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
  it("resolves the Linux TextInput host only for the Linux platform", () => {
    const modulePath = upstreamFile("Components/TextInput/LinuxTextInputNativeComponent.js");

    expect(resolveLinuxOverlayForResolvedFile(modulePath, "linux")).toBe(
      linuxOverlayIndex["Libraries/Components/TextInput/LinuxTextInputNativeComponent"],
    );
    expect(resolveLinuxOverlayForResolvedFile(modulePath, "android")).toBeNull();
    expect(resolveLinuxOverlayForResolvedFile(modulePath, "ios")).toBeNull();
  });
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
      "Libraries/Utilities/BackHandler",
      "src/private/devsupport/rndevtools/ReactDevToolsSettingsManager",
    ]);
  });
});

const expoPolyfill = (fileName: string): string =>
  path.join("/app", "node_modules", "expo-modules-core", "build", "polyfill", fileName);

describe("createLinuxResolveRequest", () => {
  it("resolves a deep import to its upstream file and substitutes the overlay for it", () => {
    const { requested, result } = resolveWith(
      { filePath: upstreamFile("Utilities/Platform.js"), type: "sourceFile" },
      "react-native/Libraries/Utilities/Platform",
    );

    expect(requested).toStrictEqual(["react-native/Libraries/Utilities/Platform"]);
    expect(result).toStrictEqual({ filePath: linuxOverlayIndex["Libraries/Utilities/Platform"], type: "sourceFile" });
  });

  it("resolves a substituted package file again, as its linux package's specifier", () => {
    const fetchIndex = path.join("/app", "node_modules", "expo", "build", "winter", "fetch", "index.js");
    const substitute = { filePath: "/app/node_modules/@react-native-linux/expo/src/fetch.ts", type: "sourceFile" };
    const requested: string[] = [];
    const context = {
      resolveRequest: (_context: unknown, name: string): unknown => {
        requested.push(name);

        return name === "./fetch" ? { filePath: fetchIndex, type: "sourceFile" } : substitute;
      },
    };

    expect(createLinuxResolveRequest(() => true)(context, "./fetch", "linux")).toBe(substitute);
    expect(requested).toStrictEqual(["./fetch", "@react-native-linux/expo/fetch"]);
  });

  it("substitutes a package overlay for the file that produced", () => {
    const { result } = resolveWith({ filePath: expoPolyfill("index.js"), type: "sourceFile" }, "./polyfill");

    expect(result).toStrictEqual({
      filePath: linuxPackageOverlayIndex["expo-modules-core/build/polyfill/index"],
      type: "sourceFile",
    });
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

/**
 * Resolves each `[originModulePath, specifier]` in turn, Metro answering `upstream.file` for `upstream.specifier` and
 * the specifier itself otherwise, and returns the origin Metro was asked to resolve each one from.
 */
const resolveOrigins = (
  upstream: { readonly file: string; readonly specifier: string },
  requests: readonly (readonly [string, string])[],
): unknown[] => {
  const origins: unknown[] = [];
  const context = {
    resolveRequest: (requestContext: { readonly originModulePath?: string }, name: string): unknown => {
      origins.push(requestContext.originModulePath);

      return { filePath: name === upstream.specifier ? upstream.file : name, type: "sourceFile" };
    },
  };
  const resolveRequest = createLinuxResolveRequest(() => false);

  for (const [originModulePath, specifier] of requests) {
    resolveRequest({ ...context, originModulePath }, specifier, "linux");
  }

  return origins;
};

describe("createLinuxResolveRequest for an overlay's own imports", () => {
  it("resolves a package overlay's own imports from the file it replaced", () => {
    const overlayPath = linuxPackageOverlayIndex["expo-modules-core/build/polyfill/index"] ?? "";
    const origins = resolveOrigins({ file: expoPolyfill("index.js"), specifier: "./polyfill" }, [
      [expoPolyfill("../index.js"), "./polyfill"],
      [overlayPath, "expo-modules-core/src/polyfill/dangerous-internal"],
    ]);

    expect(origins).toStrictEqual([expoPolyfill("../index.js"), expoPolyfill("index.js")]);
  });

  it("resolves a react-native overlay's package imports from the file it replaced and its relative ones from itself", () => {
    const overlayPath = linuxOverlayIndex["Libraries/StyleSheet/processColor"] ?? "";
    const origins = resolveOrigins({ file: upstreamFile("StyleSheet/processColor.js"), specifier: "./processColor" }, [
      [upstreamFile("StyleSheet/StyleSheet.js"), "./processColor"],
      [overlayPath, "@react-native/normalize-colors"],
      [overlayPath, "./PlatformColorValueTypes.linux.ts"],
    ]);

    expect(origins).toStrictEqual([
      upstreamFile("StyleSheet/StyleSheet.js"),
      upstreamFile("StyleSheet/processColor.js"),
      overlayPath,
    ]);
  });

  it("resolves the package imports of an overlay a deep import reached first from the file it replaced", () => {
    const overlayPath = linuxOverlayIndex["Libraries/StyleSheet/processColor"] ?? "";
    const deepImport = "react-native/Libraries/StyleSheet/processColor";
    const origins = resolveOrigins({ file: upstreamFile("StyleSheet/processColor.js"), specifier: deepImport }, [
      ["/app/src/theme.ts", deepImport],
      [overlayPath, "@react-native/normalize-colors"],
    ]);

    expect(origins).toStrictEqual(["/app/src/theme.ts", upstreamFile("StyleSheet/processColor.js")]);
  });
});

const devToolsOrigin = path.join("/app", "node_modules", "react-native", "Libraries", "Core", "setUpReactDevTools.js");
const devToolsSpecifier = "../../src/private/devsupport/rndevtools/ReactDevToolsSettingsManager";

const resolveOnly =
  (existing: string) =>
  (_context: unknown, name: string): unknown => {
    if (name !== existing) {
      throw new Error(`cannot resolve ${name}`);
    }

    return { filePath: name, type: "sourceFile" };
  };

describe("createLinuxResolveRequest for an upstream pair with no base file", () => {
  it("retries the specifier as the variant the index names", () => {
    const context = { originModulePath: devToolsOrigin, resolveRequest: resolveOnly(`${devToolsSpecifier}.android`) };

    expect(createLinuxResolveRequest(() => false)(context, devToolsSpecifier, "linux")).toStrictEqual({
      filePath: `${devToolsSpecifier}.android`,
      type: "sourceFile",
    });
  });

  it.each([
    { moduleName: "./NotListed", originModulePath: devToolsOrigin, platform: "linux" },
    { moduleName: devToolsSpecifier, originModulePath: devToolsOrigin, platform: "android" },
    { moduleName: "react-native-svg", originModulePath: devToolsOrigin, platform: "linux" },
    { moduleName: "./ReactDevToolsSettingsManager", originModulePath: "/app/src/App.js", platform: "linux" },
  ])(
    "rethrows the failure for $moduleName from $originModulePath on $platform",
    ({ moduleName, originModulePath, platform }) => {
      const context = { originModulePath, resolveRequest: resolveOnly("nothing") };

      expect(() => createLinuxResolveRequest(() => false)(context, moduleName, platform)).toThrow("cannot resolve");
    },
  );

  it("rethrows when Metro gives no origin", () => {
    const context = { resolveRequest: resolveOnly("nothing") };

    expect(() => createLinuxResolveRequest(() => false)(context, devToolsSpecifier, "linux")).toThrow("cannot resolve");
  });
});
