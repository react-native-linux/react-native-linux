import { buildCommandResult, buildEnvironment } from "./doctor-test-fixtures.ts";
import { computeExitCode, evaluateChecks } from "./evaluate-check.ts";
import { describe, expect, it } from "vitest";

import type { Tier } from "./doctor-check.ts";
import { doctorChecks } from "./doctor-checks.ts";

const ALL_TIERS: readonly Tier[] = ["core", "window", "goldens", "coverage"];
const SUCCESSFUL_EXIT_CODE = 0;
const REQUIRED_FAILURE_EXIT_CODE = 1;
const KNOWN_HEADER_FILE_NAMES = ["double-conversion.h", "core.h", "utypes.h", "version.hpp", "logging.h"];

const versionOutputByBinaryName: Readonly<Record<string, string>> = {
  clang: "clang version 99.99.99",
  "clang-18": "clang version 99.99.99",
  cmake: "cmake version 99.99.99",
  "llvm-cov": "LLVM version 99.99.99",
  "llvm-cov-18": "LLVM version 99.99.99",
  "llvm-profdata": "LLVM version 99.99.99",
  "llvm-profdata-18": "LLVM version 99.99.99",
};

const buildFullySatisfyingEnvironment = (
  versionOverrides: Readonly<Record<string, string>> = {},
): ReturnType<typeof buildEnvironment> =>
  buildEnvironment({
    hasLavapipeIcd: () => true,
    listDirectoryEntries: () => KNOWN_HEADER_FILE_NAMES,
    readEnvironmentVariable: (name) => (name === "WAYLAND_DISPLAY" ? "wayland-0" : null),
    runCommand: (name) => {
      if (name === "pkg-config") {
        return buildCommandResult("", SUCCESSFUL_EXIT_CODE);
      }

      const output = versionOverrides[name] ?? versionOutputByBinaryName[name] ?? "99.99.99";

      return buildCommandResult(output, SUCCESSFUL_EXIT_CODE);
    },
  });

describe("doctorChecks / against a fully satisfying environment", () => {
  it("passes every check and exits 0", () => {
    const results = evaluateChecks(doctorChecks, buildFullySatisfyingEnvironment(), ALL_TIERS);

    expect(results.every((result) => result.outcome === "pass")).toBe(true);
    expect(computeExitCode(results)).toBe(SUCCESSFUL_EXIT_CODE);
  });
});

describe("doctorChecks / against a machine with nothing installed", () => {
  it("fails every required check and exits 1", () => {
    const results = evaluateChecks(doctorChecks, buildEnvironment({}), ALL_TIERS);
    const requiredResults = results.filter((result) => result.check.required);

    expect(requiredResults.every((result) => result.outcome === "fail")).toBe(true);
    expect(computeExitCode(results)).toBe(REQUIRED_FAILURE_EXIT_CODE);
  });
});

describe("doctorChecks / against a machine with one present-but-too-old tool", () => {
  it("fails only CMake and still exits 1", () => {
    const environment = buildFullySatisfyingEnvironment({ cmake: "cmake version 1.0.0" });
    const results = evaluateChecks(doctorChecks, environment, ALL_TIERS);
    const cmakeResult = results.find((result) => result.check.name === "CMake");
    const ninjaResult = results.find((result) => result.check.name === "Ninja");

    expect(cmakeResult?.outcome).toBe("fail");
    expect(cmakeResult?.detail).toContain("need >=");
    expect(ninjaResult?.outcome).toBe("pass");
    expect(computeExitCode(results)).toBe(REQUIRED_FAILURE_EXIT_CODE);
  });
});
