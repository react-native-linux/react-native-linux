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
  { linux: "@react-native-linux/reanimated/expo-constants", upstream: "expo-constants" },
  { linux: "@react-native-linux/svg/expo-haptics", upstream: "expo-haptics" },
];

const resolve = (moduleName: string, platform: string | null): string | null =>
  resolveLinuxPackageAlias({ aliases, isPackageResolvable, moduleName, platform });

describe("packageAliases", () => {
  it("registers the gesture-handler and reanimated overlays and the expo-constants shim, and nothing else", () => {
    expect(packageAliases).toStrictEqual([
      { linux: "@react-native-linux/gesture-handler", upstream: "react-native-gesture-handler" },
      { linux: "@react-native-linux/reanimated", upstream: "react-native-reanimated" },
      { linux: "@react-native-linux/expo/expo-constants", upstream: "expo-constants" },
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

  it("maps a package to a subpath of an installed linux package", () => {
    expect(resolve("expo-constants", "linux")).toBe("@react-native-linux/reanimated/expo-constants");
  });

  it("returns null for a subpath alias whose linux package is not installed", () => {
    expect(resolve("expo-haptics", "linux")).toBeNull();
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
