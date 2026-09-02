import { describe, expect, it } from "vitest";
import { describeSeverity, formatJsonReport, formatReport, parseDistroId, pickRemedyLines } from "./format-report.ts";

import type { CheckResult } from "./evaluate-check.ts";
import { buildCheck } from "./doctor-test-fixtures.ts";

const SUCCESSFUL_EXIT_CODE = 0;
const REQUIRED_FAILURE_EXIT_CODE = 1;

const buildResult = (overrides: Partial<CheckResult>): CheckResult => ({
  check: buildCheck({}),
  detail: "fixture detail",
  outcome: "pass",
  ...overrides,
});

describe("describeSeverity", () => {
  it("reports PASS for a passing check", () => {
    expect(describeSeverity(buildResult({ outcome: "pass" }))).toBe("PASS");
  });

  it("reports FAIL for a failing required check", () => {
    const check = buildCheck({ required: true });

    expect(describeSeverity(buildResult({ check, outcome: "fail" }))).toBe("FAIL");
  });

  it("reports WARN for a failing optional check", () => {
    const check = buildCheck({ required: false });

    expect(describeSeverity(buildResult({ check, outcome: "fail" }))).toBe("WARN");
  });
});

describe("parseDistroId", () => {
  it("recognises Arch Linux", () => {
    expect(parseDistroId('NAME="Arch Linux"\nID=arch\n')).toBe("arch");
  });

  it("recognises quoted ids", () => {
    expect(parseDistroId('ID="ubuntu"\n')).toBe("ubuntu");
  });

  it("treats debian as the ubuntu remedy family", () => {
    expect(parseDistroId("ID=debian\n")).toBe("ubuntu");
  });

  it("falls back to unknown for an unrecognised id", () => {
    expect(parseDistroId("ID=fedora\n")).toBe("unknown");
  });

  it("falls back to unknown when there is no ID line", () => {
    expect(parseDistroId("NAME=Fixture\n")).toBe("unknown");
  });
});

describe("pickRemedyLines", () => {
  const remedy = { arch: "pacman fixture", ubuntu: "apt fixture" };

  it("shows only the Arch remedy on Arch", () => {
    expect(pickRemedyLines(remedy, "arch")).toEqual(["Arch: pacman fixture"]);
  });

  it("shows only the Ubuntu remedy on Ubuntu", () => {
    expect(pickRemedyLines(remedy, "ubuntu")).toEqual(["Ubuntu: apt fixture"]);
  });

  it("shows both remedies when the distro is unknown", () => {
    expect(pickRemedyLines(remedy, "unknown")).toEqual(["Arch: pacman fixture", "Ubuntu: apt fixture"]);
  });
});

describe("formatReport / per-check lines", () => {
  it("renders a passing check without a why line or remedy", () => {
    const check = buildCheck({ name: "CMake", tier: "core" });
    const report = formatReport([buildResult({ check, detail: "found cmake 3.28.3", outcome: "pass" })], "arch");

    expect(report).toContain("core:");
    expect(report).toContain("[PASS] CMake — found cmake 3.28.3");
    expect(report).not.toContain("why:");
  });

  it("renders a failing required check with its why line and remedy", () => {
    const check = buildCheck({ name: "CMake", required: true, tier: "core", why: "needed to configure" });
    const report = formatReport([buildResult({ check, detail: "not found", outcome: "fail" })], "arch");

    expect(report).toContain("[FAIL] CMake — not found");
    expect(report).toContain("why: needed to configure");
    expect(report).toContain("Arch: fixture arch remedy");
  });

  it("renders a failing optional check as a warning", () => {
    const check = buildCheck({ name: "ccache", required: false, tier: "core" });
    const report = formatReport([buildResult({ check, outcome: "fail" })], "ubuntu");

    expect(report).toContain("[WARN] ccache");
    expect(report).toContain("Ubuntu: fixture ubuntu remedy");
  });
});

describe("formatReport / sections and summary", () => {
  it("orders sections by the fixed tier order and skips empty tiers", () => {
    const windowCheck = buildCheck({ name: "window check", tier: "window" });
    const coreCheck = buildCheck({ name: "core check", tier: "core" });
    const report = formatReport([buildResult({ check: windowCheck }), buildResult({ check: coreCheck })], "unknown");
    const coreIndex = report.indexOf("core:");
    const windowIndex = report.indexOf("window:");

    expect(coreIndex).toBeGreaterThanOrEqual(SUCCESSFUL_EXIT_CODE);
    expect(windowIndex).toBeGreaterThan(coreIndex);
    expect(report).not.toContain("goldens:");
    expect(report).not.toContain("coverage:");
  });

  it("summarises passed, warned and failed counts", () => {
    const passing = buildResult({ check: buildCheck({ name: "passing" }), outcome: "pass" });
    const warning = buildResult({ check: buildCheck({ name: "warning", required: false }), outcome: "fail" });
    const failing = buildResult({ check: buildCheck({ name: "failing", required: true }), outcome: "fail" });

    expect(formatReport([passing, warning, failing], "arch")).toContain("1 passed, 1 warned, 1 failed");
  });
});

describe("formatJsonReport", () => {
  it("serialises each result with its severity and the given exit code", () => {
    const check = buildCheck({ name: "CMake", required: true, tier: "core", why: "needed to configure" });
    const json = formatJsonReport(
      [buildResult({ check, detail: "found cmake 3.28.3", outcome: "pass" })],
      SUCCESSFUL_EXIT_CODE,
    );
    const parsed: unknown = JSON.parse(json);

    expect(parsed).toEqual({
      exitCode: SUCCESSFUL_EXIT_CODE,
      results: [
        {
          detail: "found cmake 3.28.3",
          name: "CMake",
          remedy: { arch: "fixture arch remedy", ubuntu: "fixture ubuntu remedy" },
          required: true,
          severity: "PASS",
          tier: "core",
          why: "needed to configure",
        },
      ],
    });
  });

  it("reports the exit code it was given even when every result passed", () => {
    const json = formatJsonReport([buildResult({})], REQUIRED_FAILURE_EXIT_CODE);
    const parsed: unknown = JSON.parse(json);

    expect(parsed).toMatchObject({ exitCode: REQUIRED_FAILURE_EXIT_CODE });
  });
});
