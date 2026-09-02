import { buildCheck, buildCommandResult, buildEnvironment } from "./doctor-test-fixtures.ts";
import { computeExitCode, evaluateCheck, evaluateChecks, isVersionAtLeast } from "./evaluate-check.ts";
import { describe, expect, it } from "vitest";

const SUCCESSFUL_EXIT_CODE = 0;
const NO_REQUIRED_FAILURES_EXIT_CODE = 0;
const REQUIRED_FAILURE_EXIT_CODE = 1;

describe("evaluateCheck / env-var probe", () => {
  it("passes and reports the value when the variable is set", () => {
    const check = buildCheck({ probe: { kind: "env-var", variableName: "WAYLAND_DISPLAY" } });
    const environment = buildEnvironment({
      readEnvironmentVariable: (name) => (name === "WAYLAND_DISPLAY" ? "wayland-0" : null),
    });

    expect(evaluateCheck(check, environment)).toEqual({ check, detail: "WAYLAND_DISPLAY=wayland-0", outcome: "pass" });
  });

  it("fails when the variable is unset", () => {
    const check = buildCheck({ probe: { kind: "env-var", variableName: "WAYLAND_DISPLAY" } });
    const environment = buildEnvironment({ readEnvironmentVariable: () => null });

    expect(evaluateCheck(check, environment)).toEqual({ check, detail: "WAYLAND_DISPLAY is not set", outcome: "fail" });
  });

  it("fails when the variable is set but empty", () => {
    const check = buildCheck({ probe: { kind: "env-var", variableName: "WAYLAND_DISPLAY" } });
    const environment = buildEnvironment({ readEnvironmentVariable: () => "" });

    expect(evaluateCheck(check, environment).outcome).toBe("fail");
  });
});

describe("evaluateCheck / lavapipe-icd probe", () => {
  it("passes when a lavapipe ICD is found", () => {
    const check = buildCheck({ probe: { kind: "lavapipe-icd" } });
    const environment = buildEnvironment({ hasLavapipeIcd: () => true });

    expect(evaluateCheck(check, environment)).toEqual({
      check,
      detail: "found a lavapipe ICD manifest or libvulkan_lvp.so",
      outcome: "pass",
    });
  });

  it("fails when no lavapipe ICD is found", () => {
    const check = buildCheck({ probe: { kind: "lavapipe-icd" } });
    const environment = buildEnvironment({ hasLavapipeIcd: () => false });

    expect(evaluateCheck(check, environment)).toEqual({
      check,
      detail: "no lavapipe ICD manifest or libvulkan_lvp.so found",
      outcome: "fail",
    });
  });
});

describe("evaluateChecks", () => {
  it("only evaluates checks in the selected tiers", () => {
    const coreCheck = buildCheck({ name: "core check", tier: "core" });
    const windowCheck = buildCheck({ name: "window check", tier: "window" });
    const goldensCheck = buildCheck({ name: "goldens check", tier: "goldens" });
    const environment = buildEnvironment({ runCommand: () => buildCommandResult("1", SUCCESSFUL_EXIT_CODE) });
    const results = evaluateChecks([coreCheck, windowCheck, goldensCheck], environment, ["window"]);

    expect(results.map((result) => result.check.name)).toEqual(["window check"]);
  });

  it("evaluates no checks when no tier is selected", () => {
    const coreCheck = buildCheck({ tier: "core" });
    const environment = buildEnvironment({});

    expect(evaluateChecks([coreCheck], environment, [])).toEqual([]);
  });

  it("evaluates checks from every selected tier", () => {
    const coreCheck = buildCheck({ name: "core check", tier: "core" });
    const windowCheck = buildCheck({ name: "window check", tier: "window" });
    const environment = buildEnvironment({ runCommand: () => buildCommandResult("1", SUCCESSFUL_EXIT_CODE) });
    const results = evaluateChecks([coreCheck, windowCheck], environment, ["core", "window"]);

    expect(results.map((result) => result.check.name)).toEqual(["core check", "window check"]);
  });
});

describe("computeExitCode", () => {
  it("returns 0 when every check passes", () => {
    const check = buildCheck({});

    expect(computeExitCode([{ check, detail: "", outcome: "pass" }])).toBe(NO_REQUIRED_FAILURES_EXIT_CODE);
  });

  it("returns 0 when only optional checks fail", () => {
    const check = buildCheck({ required: false });

    expect(computeExitCode([{ check, detail: "", outcome: "fail" }])).toBe(NO_REQUIRED_FAILURES_EXIT_CODE);
  });

  it("returns 1 when a required check fails", () => {
    const check = buildCheck({ required: true });

    expect(computeExitCode([{ check, detail: "", outcome: "fail" }])).toBe(REQUIRED_FAILURE_EXIT_CODE);
  });

  it("returns 1 when a required check fails alongside passing ones", () => {
    const passingCheck = buildCheck({ name: "passing", required: true });
    const failingCheck = buildCheck({ name: "failing", required: true });
    const results = [
      { check: passingCheck, detail: "", outcome: "pass" as const },
      { check: failingCheck, detail: "", outcome: "fail" as const },
    ];

    expect(computeExitCode(results)).toBe(REQUIRED_FAILURE_EXIT_CODE);
  });

  it("returns 0 for an empty result set", () => {
    expect(computeExitCode([])).toBe(NO_REQUIRED_FAILURES_EXIT_CODE);
  });
});

describe("isVersionAtLeast", () => {
  it("is true when the versions are equal", () => {
    expect(isVersionAtLeast("3.28.0", "3.28.0")).toBe(true);
  });

  it("is true when the found major version is greater", () => {
    expect(isVersionAtLeast("4.0.0", "3.28.0")).toBe(true);
  });

  it("is false when the found minor version is lower", () => {
    expect(isVersionAtLeast("3.27.9", "3.28.0")).toBe(false);
  });

  it("is true when the found version has more parts but is equal", () => {
    expect(isVersionAtLeast("3.28.0", "3.28")).toBe(true);
  });

  it("is true when the found version has fewer parts but is equal", () => {
    expect(isVersionAtLeast("3.28", "3.28.0")).toBe(true);
  });

  it("is false when the found patch version is lower", () => {
    expect(isVersionAtLeast("3.28.0", "3.28.1")).toBe(false);
  });
});
