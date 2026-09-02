import { buildCheck, buildCommandResult, buildEnvironment } from "./doctor-test-fixtures.ts";
import { describe, expect, it } from "vitest";

import { evaluateCheck } from "./evaluate-check.ts";

const SUCCESSFUL_EXIT_CODE = 0;
const FAILING_EXIT_CODE = 1;

describe("evaluateCheck / binary probe: presence", () => {
  it("passes without checking a version when no minimum is declared", () => {
    const check = buildCheck({
      probe: {
        candidateNames: ["ninja"],
        kind: "binary",
        versionArguments: ["--version"],
        versionPattern: /(?<version>\d+)/u,
      },
    });
    const environment = buildEnvironment({
      runCommand: (name) => (name === "ninja" ? buildCommandResult("1.11.0", SUCCESSFUL_EXIT_CODE) : null),
    });

    expect(evaluateCheck(check, environment)).toEqual({ check, detail: "found ninja", outcome: "pass" });
  });

  const pythonProbe = {
    candidateNames: ["python3", "python"],
    kind: "binary" as const,
    versionArguments: ["--version"],
    versionPattern: /(?<version>\d+)/u,
  };

  it("fails when none of the candidate names are found", () => {
    const check = buildCheck({ probe: pythonProbe });
    const environment = buildEnvironment({ runCommand: () => null });
    const result = evaluateCheck(check, environment);

    expect(result.outcome).toBe("fail");
    expect(result.detail).toBe('"python3" or "python" was not found on PATH');
  });

  it("falls through to the second candidate name when the first is missing", () => {
    const check = buildCheck({ probe: pythonProbe });
    const environment = buildEnvironment({
      runCommand: (name) => (name === "python" ? buildCommandResult("3.12.0", SUCCESSFUL_EXIT_CODE) : null),
    });

    expect(evaluateCheck(check, environment)).toEqual({ check, detail: "found python", outcome: "pass" });
  });
});

const cmakeProbe = {
  candidateNames: ["cmake"],
  kind: "binary" as const,
  minimumVersion: "3.28.0",
  versionArguments: ["--version"],
  versionPattern: /cmake version (?<version>\d+\.\d+\.\d+)/u,
};

describe("evaluateCheck / binary probe: version at or above the minimum", () => {
  it("passes when the found version meets the minimum", () => {
    const check = buildCheck({ probe: cmakeProbe });
    const environment = buildEnvironment({
      runCommand: () => buildCommandResult("cmake version 3.28.3", SUCCESSFUL_EXIT_CODE),
    });

    expect(evaluateCheck(check, environment)).toEqual({ check, detail: "found cmake 3.28.3", outcome: "pass" });
  });

  it("passes when the found version is newer than the minimum", () => {
    const check = buildCheck({ probe: cmakeProbe });
    const environment = buildEnvironment({
      runCommand: () => buildCommandResult("cmake version 4.0.0", SUCCESSFUL_EXIT_CODE),
    });

    expect(evaluateCheck(check, environment).outcome).toBe("pass");
  });
});

describe("evaluateCheck / binary probe: version below the minimum or unparsable", () => {
  it("fails when the found version is older than the minimum", () => {
    const check = buildCheck({ probe: cmakeProbe });
    const environment = buildEnvironment({
      runCommand: () => buildCommandResult("cmake version 3.27.9", SUCCESSFUL_EXIT_CODE),
    });

    expect(evaluateCheck(check, environment)).toEqual({
      check,
      detail: "found cmake 3.27.9, need >= 3.28.0",
      outcome: "fail",
    });
  });

  it("fails when the version cannot be parsed from the command output", () => {
    const check = buildCheck({ probe: cmakeProbe });
    const environment = buildEnvironment({
      runCommand: () => buildCommandResult("not a version string", SUCCESSFUL_EXIT_CODE),
    });

    expect(evaluateCheck(check, environment)).toEqual({
      check,
      detail: "found cmake but could not parse its version",
      outcome: "fail",
    });
  });
});

describe("evaluateCheck / binary probe: override environment variable", () => {
  const llvmCovProbe = {
    candidateNames: ["llvm-cov", "llvm-cov-18"],
    kind: "binary" as const,
    overrideEnvironmentVariable: "LLVM_COV",
    versionArguments: ["--version"],
    versionPattern: /(?<version>\d+)/u,
  };

  it("uses the override environment variable when it names a working binary", () => {
    const check = buildCheck({ probe: llvmCovProbe });
    const environment = buildEnvironment({
      readEnvironmentVariable: (name) => (name === "LLVM_COV" ? "llvm-cov-custom" : null),
      runCommand: (name) => (name === "llvm-cov-custom" ? buildCommandResult("1", SUCCESSFUL_EXIT_CODE) : null),
    });

    expect(evaluateCheck(check, environment)).toEqual({ check, detail: "found llvm-cov-custom", outcome: "pass" });
  });

  it("falls back to the candidate names when the override variable is unset", () => {
    const check = buildCheck({ probe: llvmCovProbe });
    const environment = buildEnvironment({
      readEnvironmentVariable: () => null,
      runCommand: (name) => (name === "llvm-cov-18" ? buildCommandResult("1", SUCCESSFUL_EXIT_CODE) : null),
    });

    expect(evaluateCheck(check, environment)).toEqual({ check, detail: "found llvm-cov-18", outcome: "pass" });
  });

  it("falls back to the candidate names when the override variable is empty", () => {
    const check = buildCheck({ probe: llvmCovProbe });
    const environment = buildEnvironment({
      readEnvironmentVariable: () => "",
      runCommand: (name) => (name === "llvm-cov" ? buildCommandResult("1", SUCCESSFUL_EXIT_CODE) : null),
    });

    expect(evaluateCheck(check, environment)).toEqual({ check, detail: "found llvm-cov", outcome: "pass" });
  });
});

describe("evaluateCheck / pkg-config probe", () => {
  it("passes when pkg-config reports the module exists", () => {
    const check = buildCheck({ probe: { kind: "pkg-config", moduleName: "wayland-client" } });
    const environment = buildEnvironment({
      runCommand: (name, commandArguments) =>
        name === "pkg-config" && commandArguments.includes("wayland-client")
          ? buildCommandResult("", SUCCESSFUL_EXIT_CODE)
          : null,
    });

    expect(evaluateCheck(check, environment)).toEqual({
      check,
      detail: 'pkg-config found "wayland-client"',
      outcome: "pass",
    });
  });

  it("fails when pkg-config is not on PATH", () => {
    const check = buildCheck({ probe: { kind: "pkg-config", moduleName: "wayland-client" } });
    const environment = buildEnvironment({ runCommand: () => null });

    expect(evaluateCheck(check, environment)).toEqual({
      check,
      detail: 'pkg-config could not find "wayland-client"',
      outcome: "fail",
    });
  });

  it("fails when pkg-config exits non-zero", () => {
    const check = buildCheck({ probe: { kind: "pkg-config", moduleName: "wayland-client" } });
    const environment = buildEnvironment({ runCommand: () => buildCommandResult("", FAILING_EXIT_CODE) });

    expect(evaluateCheck(check, environment).outcome).toBe("fail");
  });
});

describe("evaluateCheck / file-glob probe", () => {
  it("passes when a matching file is found in the first directory", () => {
    const check = buildCheck({
      probe: { fileNamePattern: /^core\.h$/u, kind: "file-glob", searchDirectories: ["/usr/include/fmt"] },
    });
    const environment = buildEnvironment({
      listDirectoryEntries: (directoryPath) => (directoryPath === "/usr/include/fmt" ? ["core.h", "format.h"] : []),
    });

    expect(evaluateCheck(check, environment)).toEqual({
      check,
      detail: "found under /usr/include/fmt",
      outcome: "pass",
    });
  });

  it("passes when a matching file is found only in a later directory", () => {
    const check = buildCheck({
      probe: {
        fileNamePattern: /^version\.hpp$/u,
        kind: "file-glob",
        searchDirectories: ["/usr/local/include/boost", "/usr/include/boost"],
      },
    });
    const environment = buildEnvironment({
      listDirectoryEntries: (directoryPath) => (directoryPath === "/usr/include/boost" ? ["version.hpp"] : []),
    });

    expect(evaluateCheck(check, environment)).toEqual({
      check,
      detail: "found under /usr/include/boost",
      outcome: "pass",
    });
  });

  it("fails when no directory has a matching file", () => {
    const check = buildCheck({
      probe: { fileNamePattern: /^utypes\.h$/u, kind: "file-glob", searchDirectories: ["/usr/include/unicode"] },
    });
    const environment = buildEnvironment({ listDirectoryEntries: () => [] });

    expect(evaluateCheck(check, environment)).toEqual({
      check,
      detail: "no file matching ^utypes\\.h$ under /usr/include/unicode",
      outcome: "fail",
    });
  });
});
