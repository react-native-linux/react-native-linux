import type { DirectoryEntry, FormatCppEnvironment } from "./format-cpp-types.ts";

import {
  EXPECTED_CLANG_FORMAT_VERSION_PREFIX,
  buildClangFormatArguments,
  collectAllSourceFilePaths,
  resolveClangFormatBinary,
  runFormatCpp,
} from "./run-format-cpp.ts";
import { describe, expect, it, vi } from "vitest";

const REPOSITORY_ROOT = "/repo";
const SUCCESSFUL_EXIT_CODE = 0;
const FAILING_EXIT_CODE = 1;
const OTHER_FAILING_EXIT_CODE = 2;
const EMPTY_SEGMENT_LENGTH = 0;
const SEGMENTS_AFTER_LAST = 1;
const FIRST_REPORT_INDEX = 0;

interface RunCall {
  readonly args: readonly string[];
  readonly binaryName: string;
}

interface PathSegmentTarget {
  readonly entriesByDirectory: Map<string, DirectoryEntry[]>;
  readonly seenPaths: Set<string>;
}

const seenPathAlready = (target: PathSegmentTarget, currentPath: string): boolean => {
  if (target.seenPaths.has(currentPath)) {
    return true;
  }
  target.seenPaths.add(currentPath);
  return false;
};

const addPathSegment = (target: PathSegmentTarget, parentPath: string, entry: DirectoryEntry): void => {
  const currentPath = `${parentPath}/${entry.name}`;
  if (seenPathAlready(target, currentPath)) {
    return;
  }

  const entries = target.entriesByDirectory.get(parentPath) ?? [];
  entries.push(entry);
  target.entriesByDirectory.set(parentPath, entries);
};

const buildFileSystem = (files: readonly string[]): FormatCppEnvironment["listDirectory"] => {
  const target: PathSegmentTarget = { entriesByDirectory: new Map(), seenPaths: new Set() };

  for (const filePath of files) {
    const segments = filePath.split("/").filter((segment) => segment.length > EMPTY_SEGMENT_LENGTH);
    let parentPath = "";

    for (const [index, name] of segments.entries()) {
      addPathSegment(target, parentPath, { isDirectory: index < segments.length - SEGMENTS_AFTER_LAST, name });
      parentPath = `${parentPath}/${name}`;
    }
  }

  return (directoryPath: string) => target.entriesByDirectory.get(directoryPath) ?? [];
};

const ignoredReports: string[] = [];
const NOOP_REPORT = (message: string): void => {
  ignoredReports.push(message);
};

const buildEnvironment = (overrides: Partial<FormatCppEnvironment> = {}): FormatCppEnvironment => ({
  listDirectory: () => [],
  readClangFormatVersion: (binaryName) => (binaryName === "clang-format-18" ? "18.1.8" : null),
  report: NOOP_REPORT,
  runClangFormat: () => SUCCESSFUL_EXIT_CODE,
  ...overrides,
});

describe("collectAllSourceFilePaths", () => {
  it("finds .cpp and .h files recursively, ignoring other extensions", () => {
    const listDirectory = buildFileSystem([
      "/repo/packages/core/src/Scene.cpp",
      "/repo/packages/core/src/Scene.h",
      "/repo/packages/core/src/Scene.md",
      "/repo/packages/core/src/text/TextLayout.cpp",
    ]);

    expect(collectAllSourceFilePaths(["/repo/packages/core/src"], listDirectory)).toStrictEqual([
      "/repo/packages/core/src/Scene.cpp",
      "/repo/packages/core/src/Scene.h",
      "/repo/packages/core/src/text/TextLayout.cpp",
    ]);
  });

  it("excludes generated, vendored and dependency directories", () => {
    const listDirectory = buildFileSystem([
      "/repo/packages/core/src/Scene.cpp",
      "/repo/packages/core/src/generated/Spec.h",
      "/repo/packages/core/src/upstream/Vendored.cpp",
      "/repo/packages/core/src/third_party/Vendored.h",
      "/repo/packages/core/src/node_modules/Vendored.cpp",
    ]);

    expect(collectAllSourceFilePaths(["/repo/packages/core/src"], listDirectory)).toStrictEqual([
      "/repo/packages/core/src/Scene.cpp",
    ]);
  });

  it("merges and sorts files across multiple directories", () => {
    const listDirectory = buildFileSystem([
      "/repo/packages/core/tests/SceneTest.cpp",
      "/repo/packages/core/src/Scene.cpp",
    ]);

    expect(
      collectAllSourceFilePaths(["/repo/packages/core/src", "/repo/packages/core/tests"], listDirectory),
    ).toStrictEqual(["/repo/packages/core/src/Scene.cpp", "/repo/packages/core/tests/SceneTest.cpp"]);
  });

  it("returns an empty list when no directories are given", () => {
    const listDirectory = buildFileSystem([]);

    expect(collectAllSourceFilePaths([], listDirectory)).toStrictEqual([]);
  });
});

describe("resolveClangFormatBinary", () => {
  it("picks the first candidate whose reported version matches the pinned prefix", () => {
    const readClangFormatVersion = vi.fn((binaryName: string) => (binaryName === "clang-format-18" ? "18.1.8" : null));

    expect(resolveClangFormatBinary(readClangFormatVersion)).toStrictEqual({
      binaryName: "clang-format-18",
      version: "18.1.8",
    });
  });

  it("falls back to the next candidate when the first is missing", () => {
    const readClangFormatVersion = vi.fn((binaryName: string) => (binaryName === "clang-format" ? "18.1.3" : null));

    expect(resolveClangFormatBinary(readClangFormatVersion)).toStrictEqual({
      binaryName: "clang-format",
      version: "18.1.3",
    });
  });

  it("rejects a candidate reporting the wrong major version", () => {
    const readClangFormatVersion = vi.fn(() => "20.1.0");

    expect(resolveClangFormatBinary(readClangFormatVersion)).toBeNull();
  });

  it("returns null when no candidate is on PATH", () => {
    const readClangFormatVersion = vi.fn(() => null);

    expect(resolveClangFormatBinary(readClangFormatVersion)).toBeNull();
  });
});

describe("buildClangFormatArguments", () => {
  it("builds a dry-run, warnings-as-errors invocation for check mode", () => {
    expect(buildClangFormatArguments("check", ["/repo/a.cpp", "/repo/b.h"])).toStrictEqual([
      "--dry-run",
      "--Werror",
      "--style=file",
      "/repo/a.cpp",
      "/repo/b.h",
    ]);
  });

  it("builds an in-place invocation for write mode", () => {
    expect(buildClangFormatArguments("write", ["/repo/a.cpp"])).toStrictEqual(["-i", "--style=file", "/repo/a.cpp"]);
  });
});

describe("EXPECTED_CLANG_FORMAT_VERSION_PREFIX", () => {
  it("names the CI-pinned major version", () => {
    expect(EXPECTED_CLANG_FORMAT_VERSION_PREFIX).toBe("18.");
  });
});

const buildRecordingClangFormat = (
  exitCode: number,
): { calls: RunCall[]; runClangFormat: FormatCppEnvironment["runClangFormat"] } => {
  const calls: RunCall[] = [];

  return {
    calls,
    runClangFormat: (binaryName, args) => {
      calls.push({ args, binaryName });
      return exitCode;
    },
  };
};

describe("runFormatCpp, when no pinned clang-format is on PATH", () => {
  it("reports the expected version and refuses to run clang-format", () => {
    const reports: string[] = [];
    const { calls, runClangFormat } = buildRecordingClangFormat(SUCCESSFUL_EXIT_CODE);
    const environment = buildEnvironment({
      readClangFormatVersion: () => null,
      report: (message) => {
        reports.push(message);
      },
      runClangFormat,
    });

    expect(runFormatCpp("check", environment, REPOSITORY_ROOT)).toBe(FAILING_EXIT_CODE);
    expect(calls).toStrictEqual([]);
    expect(reports[FIRST_REPORT_INDEX]).toContain(EXPECTED_CLANG_FORMAT_VERSION_PREFIX);
  });
});

describe("runFormatCpp, when a pinned clang-format is found", () => {
  it("runs it over the two source directories and returns its exit code", () => {
    const listDirectory = buildFileSystem(["/repo/packages/core/src/Scene.cpp"]);
    const { calls, runClangFormat } = buildRecordingClangFormat(SUCCESSFUL_EXIT_CODE);
    const environment = buildEnvironment({ listDirectory, runClangFormat });

    expect(runFormatCpp("write", environment, REPOSITORY_ROOT)).toBe(SUCCESSFUL_EXIT_CODE);
    expect(calls).toStrictEqual([
      {
        args: ["-i", "--style=file", "/repo/packages/core/src/Scene.cpp"],
        binaryName: "clang-format-18",
      },
    ]);
  });

  it("propagates a non-zero exit code as failure", () => {
    const environment = buildEnvironment({ runClangFormat: () => FAILING_EXIT_CODE });

    expect(runFormatCpp("check", environment, REPOSITORY_ROOT)).toBe(FAILING_EXIT_CODE);
  });

  it("treats any non-zero exit code the same way", () => {
    const environment = buildEnvironment({ runClangFormat: () => OTHER_FAILING_EXIT_CODE });

    expect(runFormatCpp("check", environment, REPOSITORY_ROOT)).toBe(FAILING_EXIT_CODE);
  });
});
