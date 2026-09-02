import { describe, expect, it } from "vitest";

import {
  linuxOverlayIndex,
  resolveAgainstFilesystem,
  resolveLinuxOverlay,
  resolvePlatformCandidates,
} from "./metro-config.ts";

import { existsSync } from "node:fs";
import path from "node:path";

const overlayIndex = {
  "Libraries/Utilities/Platform": "/repo/packages/core/src-linux/Libraries/Utilities/Platform.linux.ts",
};

const resolutionFixturesDirectory = path.join(import.meta.dirname, "..", "test-fixtures", "resolution");
const fixtureModulePath = (moduleName: string): string => path.join(resolutionFixturesDirectory, moduleName);

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
