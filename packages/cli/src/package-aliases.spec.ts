import { describe, expect, it } from "vitest";
import { packageAliases, resolveLinuxPackageAlias } from "./package-aliases.ts";

import { existsSync } from "node:fs";
import path from "node:path";

const fixturesDirectory = path.join(import.meta.dirname, "..", "test-fixtures", "package-aliases");

const isPackageResolvable = (packageName: string): boolean =>
  existsSync(path.join(fixturesDirectory, packageName, "package.json"));

const aliases = [
  { linux: "@react-native-linux/reanimated", upstream: "react-native-reanimated" },
  { linux: "@react-native-linux/svg", upstream: "react-native-svg" },
];

const resolve = (moduleName: string, platform: string | null): string | null =>
  resolveLinuxPackageAlias({ aliases, isPackageResolvable, moduleName, platform });

describe("packageAliases", () => {
  it("registers the reanimated overlay and nothing else", () => {
    expect(packageAliases).toStrictEqual([
      { linux: "@react-native-linux/reanimated", upstream: "react-native-reanimated" },
    ]);
  });
});

describe("resolveLinuxPackageAlias", () => {
  it("maps an aliased upstream package to the linux overlay package", () => {
    expect(resolve("react-native-reanimated", "linux")).toBe("@react-native-linux/reanimated");
  });

  it("keeps the subpath of a deep import into an aliased package", () => {
    expect(resolve("react-native-reanimated/plugin", "linux")).toBe("@react-native-linux/reanimated/plugin");
  });

  it("returns null when the aliased overlay package is not installed", () => {
    expect(resolve("react-native-svg", "linux")).toBeNull();
  });

  it("returns null for a package that has no alias", () => {
    expect(resolve("react-native-gesture-handler", "linux")).toBeNull();
  });

  it("returns null for a package whose name only starts with an aliased name", () => {
    expect(resolve("react-native-reanimated-extra", "linux")).toBeNull();
  });

  it("returns null on every platform other than linux", () => {
    expect(resolve("react-native-reanimated", "android")).toBeNull();
  });

  it("returns null when the platform is not set", () => {
    expect(resolve("react-native-reanimated", null)).toBeNull();
  });
});
