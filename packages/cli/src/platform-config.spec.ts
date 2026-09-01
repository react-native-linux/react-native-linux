import { describe, expect, it } from "vitest";

import path from "node:path";

import { platformConfig } from "./platform-config.ts";

const fixturesRoot = path.join(import.meta.dirname, "..", "test-fixtures");
const applicationRoot = path.join(fixturesRoot, "application");
const libraryRoot = path.join(fixturesRoot, "library");

const { dependencyConfig, npmPackageName, projectConfig } = platformConfig.platforms.linux;

describe("platformConfig", () => {
  it("registers the linux platform against the core package", () => {
    expect(npmPackageName).toBe("@react-native-linux/core");
  });

  it("resolves the conventional source directory of a project", () => {
    expect(projectConfig(applicationRoot, {})).toEqual({ sourceDir: path.join(applicationRoot, "linux") });
  });

  it("resolves an explicitly configured source directory of a dependency", () => {
    expect(dependencyConfig(applicationRoot, { sourceDir: "linux" })).toEqual({
      sourceDir: path.join(applicationRoot, "linux"),
    });
  });

  it("reports no project for a package without a linux source directory", () => {
    expect(projectConfig(libraryRoot, {})).toBeNull();
  });

  it("reports no dependency for a configured source directory that does not exist", () => {
    expect(dependencyConfig(applicationRoot, { sourceDir: "missing" })).toBeNull();
  });
});
