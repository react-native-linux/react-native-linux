import { describe, expect, it } from "vitest";

import {
  linuxOverlayIndex,
  resolveAgainstFilesystem,
  resolveLinuxModuleName,
  resolveLinuxOverlay,
  resolveOriginAwareCandidates,
  resolvePlatformCandidates,
  shouldUseJavaScriptFallback,
} from "./metro-config.ts";

import { existsSync } from "node:fs";
import path from "node:path";

const overlayIndex = {
  "Libraries/Utilities/Platform": "/repo/packages/core/src-linux/Libraries/Utilities/Platform.linux.ts",
};

const resolutionFixturesDirectory = path.join(import.meta.dirname, "..", "test-fixtures", "resolution");
const fixtureModulePath = (moduleName: string): string => path.join(resolutionFixturesDirectory, moduleName);

const fallbackFixturesDirectory = path.join(import.meta.dirname, "..", "test-fixtures", "reanimated-fallback");
const fallbackModulePath = (relativePath: string): string => path.join(fallbackFixturesDirectory, relativePath);

const reanimatedEntry = fallbackModulePath("react-native-reanimated/src/index.ts");
const workletsEntry = fallbackModulePath("react-native-worklets/src/index.ts");
const gestureHandlerEntry = fallbackModulePath("react-native-gesture-handler/src/index.ts");

interface FallbackRequest {
  readonly moduleName: string;
  readonly originModulePath: string;
  readonly platform: string;
}

const fallbackRequest = (moduleName: string, originModulePath: string, platform: string): FallbackRequest => ({
  moduleName,
  originModulePath,
  platform,
});

const isPackageResolvable = (): boolean => true;

describe("resolveLinuxOverlay", () => {
  it("resolves a react-native deep import that has a linux overlay", () => {
    expect(resolveLinuxOverlay("react-native/Libraries/Utilities/Platform", "linux", overlayIndex)).toBe(
      overlayIndex["Libraries/Utilities/Platform"],
    );
  });

  it("returns null when the platform is not linux", () => {
    expect(resolveLinuxOverlay("react-native/Libraries/Utilities/Platform", "android", overlayIndex)).toBeNull();
  });

  it("returns null when the platform is not set", () => {
    expect(resolveLinuxOverlay("react-native/Libraries/Utilities/Platform", null, overlayIndex)).toBeNull();
  });

  it("returns null for module names outside the react-native package", () => {
    expect(resolveLinuxOverlay("react-native-gesture-handler", "linux", overlayIndex)).toBeNull();
  });

  it("returns null when the requested react-native subpath has no overlay entry", () => {
    expect(resolveLinuxOverlay("react-native/Libraries/Components/View/View", "linux", overlayIndex)).toBeNull();
  });
});

describe("linuxOverlayIndex", () => {
  it("maps the tracked Platform override to its src-linux file", () => {
    expect(linuxOverlayIndex["Libraries/Utilities/Platform"]).toMatch(
      /src-linux[/\\]Libraries[/\\]Utilities[/\\]Platform\.linux\.ts$/u,
    );
  });

  it("maps the tracked PlatformTypes override to its src-linux file", () => {
    expect(linuxOverlayIndex["Libraries/Utilities/PlatformTypes"]).toMatch(
      /src-linux[/\\]Libraries[/\\]Utilities[/\\]PlatformTypes\.ts$/u,
    );
  });
});

describe("resolveLinuxModuleName", () => {
  it("rewrites a react-native subpath the overlay index tracks", () => {
    expect(resolveLinuxModuleName("react-native/Libraries/Utilities/Platform", "linux", isPackageResolvable)).toMatch(
      /src-linux[/\\]Libraries[/\\]Utilities[/\\]Platform\.linux\.ts$/u,
    );
  });

  it("leaves a package name alone while the shipped alias registry is empty", () => {
    expect(resolveLinuxModuleName("react-native-reanimated", "linux", isPackageResolvable)).toBeNull();
  });
});

describe("resolvePlatformCandidates", () => {
  it("cycles the linux, native, and default suffixes once per source extension, in order", () => {
    expect(resolvePlatformCandidates("mod", "linux", ["ts", "js"])).toEqual([
      "mod.linux.ts",
      "mod.native.ts",
      "mod.ts",
      "mod.linux.js",
      "mod.native.js",
      "mod.js",
    ]);
  });

  it("never produces a web candidate for the linux platform", () => {
    expect(resolvePlatformCandidates("mod", "linux", ["ts"])).not.toContain("mod.web.ts");
  });
});

describe("resolveAgainstFilesystem", () => {
  it("picks the linux variant over native and default when all three exist on disk", () => {
    const candidates = resolvePlatformCandidates(fixtureModulePath("foo"), "linux", ["ts"]);

    expect(resolveAgainstFilesystem(candidates, existsSync)).toBe(fixtureModulePath("foo.linux.ts"));
  });

  it("picks the native variant over the default when linux is absent", () => {
    const candidates = resolvePlatformCandidates(fixtureModulePath("bar"), "linux", ["ts"]);

    expect(resolveAgainstFilesystem(candidates, existsSync)).toBe(fixtureModulePath("bar.native.ts"));
  });

  it("picks the default variant when neither linux nor native exist", () => {
    const candidates = resolvePlatformCandidates(fixtureModulePath("baz"), "linux", ["ts"]);

    expect(resolveAgainstFilesystem(candidates, existsSync)).toBe(fixtureModulePath("baz.ts"));
  });

  it("never resolves a web-only fixture for the linux platform", () => {
    const candidates = resolvePlatformCandidates(fixtureModulePath("qux"), "linux", ["ts"]);

    expect(resolveAgainstFilesystem(candidates, existsSync)).toBeNull();
  });

  it("exhausts every candidate in one source extension before moving to the next", () => {
    const candidates = resolvePlatformCandidates(fixtureModulePath("cycle"), "linux", ["ts", "js"]);

    expect(resolveAgainstFilesystem(candidates, existsSync)).toBe(fixtureModulePath("cycle.native.ts"));
  });
});

describe("shouldUseJavaScriptFallback", () => {
  it("claims a relative import made from inside react-native-reanimated on linux", () => {
    const request = fallbackRequest("./hook/use-animated-style", reanimatedEntry, "linux");

    expect(shouldUseJavaScriptFallback(request)).toBe(true);
  });

  it("claims a relative import made from inside react-native-worklets on linux", () => {
    const request = fallbackRequest("./worklets-module/native-worklets", workletsEntry, "linux");

    expect(shouldUseJavaScriptFallback(request)).toBe(true);
  });

  it("leaves relative imports of the same packages alone on other native platforms", () => {
    const request = fallbackRequest("./worklets-module/native-worklets", workletsEntry, "android");

    expect(shouldUseJavaScriptFallback(request)).toBe(false);
  });

  it("leaves package imports made from inside the fallback packages alone", () => {
    const request = fallbackRequest("react-native-worklets", reanimatedEntry, "linux");

    expect(shouldUseJavaScriptFallback(request)).toBe(false);
  });

  it("leaves relative imports of every other package alone", () => {
    const request = fallbackRequest("./gesture", gestureHandlerEntry, "linux");

    expect(shouldUseJavaScriptFallback(request)).toBe(false);
  });
});

describe("resolveOriginAwareCandidates", () => {
  it("skips the native suffix, and keeps the linux one, inside react-native-worklets", () => {
    const request = fallbackRequest("./worklets-module/native-worklets", workletsEntry, "linux");

    expect(resolveOriginAwareCandidates(request, ["ts", "js"])).toEqual([
      fallbackModulePath("react-native-worklets/src/worklets-module/native-worklets.linux.ts"),
      fallbackModulePath("react-native-worklets/src/worklets-module/native-worklets.ts"),
      fallbackModulePath("react-native-worklets/src/worklets-module/native-worklets.linux.js"),
      fallbackModulePath("react-native-worklets/src/worklets-module/native-worklets.js"),
    ]);
  });

  it("keeps the native suffix for a package import made from inside react-native-reanimated", () => {
    const request = fallbackRequest("react-native-worklets", reanimatedEntry, "linux");

    expect(resolveOriginAwareCandidates(request, ["ts"])).toEqual([
      "react-native-worklets.linux.ts",
      "react-native-worklets.native.ts",
      "react-native-worklets.ts",
    ]);
  });

  it("keeps the native suffix inside react-native-reanimated when the platform is not linux", () => {
    const request = fallbackRequest("./hook/use-animated-style", reanimatedEntry, "android");

    expect(resolveOriginAwareCandidates(request, ["ts"])).toEqual([
      fallbackModulePath("react-native-reanimated/src/hook/use-animated-style.android.ts"),
      fallbackModulePath("react-native-reanimated/src/hook/use-animated-style.native.ts"),
      fallbackModulePath("react-native-reanimated/src/hook/use-animated-style.ts"),
    ]);
  });
});

describe("resolveOriginAwareCandidates against the fallback fixture tree", () => {
  it("resolves a worklets module to its unsuffixed file even though a native sibling exists", () => {
    const request = fallbackRequest("./worklets-module/native-worklets", workletsEntry, "linux");
    const candidates = resolveOriginAwareCandidates(request, ["ts"]);

    expect(resolveAgainstFilesystem(candidates, existsSync)).toBe(
      fallbackModulePath("react-native-worklets/src/worklets-module/native-worklets.ts"),
    );
  });

  it("resolves a reanimated hook to its unsuffixed file even though a native sibling exists", () => {
    const request = fallbackRequest("./hook/use-animated-style", reanimatedEntry, "linux");
    const candidates = resolveOriginAwareCandidates(request, ["ts"]);

    expect(resolveAgainstFilesystem(candidates, existsSync)).toBe(
      fallbackModulePath("react-native-reanimated/src/hook/use-animated-style.ts"),
    );
  });

  it("still prefers a linux file over the unsuffixed one inside a fallback package", () => {
    const request = fallbackRequest("./hook/use-shared-value", reanimatedEntry, "linux");
    const candidates = resolveOriginAwareCandidates(request, ["ts"]);

    expect(resolveAgainstFilesystem(candidates, existsSync)).toBe(
      fallbackModulePath("react-native-reanimated/src/hook/use-shared-value.linux.ts"),
    );
  });

  it("keeps preferring the native file for a package that is not part of the fallback", () => {
    const request = fallbackRequest("./gesture", gestureHandlerEntry, "linux");
    const candidates = resolveOriginAwareCandidates(request, ["ts"]);

    expect(resolveAgainstFilesystem(candidates, existsSync)).toBe(
      fallbackModulePath("react-native-gesture-handler/src/gesture.native.ts"),
    );
  });
});
