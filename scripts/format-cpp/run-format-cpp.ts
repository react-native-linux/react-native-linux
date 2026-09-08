import type { ClangFormatBinary, FormatCppEnvironment, FormatCppMode } from "./format-cpp-types.ts";
import path from "node:path";

// Pinned to the CI `validate` job's clang-format-18 (.github/workflows/ci.yml) so every queued branch reformats against the same version.
const EXPECTED_CLANG_FORMAT_VERSION_PREFIX = "18.";

const CANDIDATE_BINARY_NAMES: readonly string[] = ["clang-format-18", "clang-format"];

const SOURCE_FILE_EXTENSIONS = new Set([".cpp", ".h"]);
const EXCLUDED_DIRECTORY_NAMES = new Set(["generated", "node_modules", "third_party", "upstream"]);

const SUCCESSFUL_EXIT_STATUS = 0;
const FAILURE_EXIT_STATUS = 1;

const collectSourceFilePaths = (
  directoryPath: string,
  listDirectory: FormatCppEnvironment["listDirectory"],
): string[] => {
  const filePaths: string[] = [];

  for (const entry of listDirectory(directoryPath)) {
    const entryPath = path.join(directoryPath, entry.name);

    if (entry.isDirectory) {
      if (!EXCLUDED_DIRECTORY_NAMES.has(entry.name)) {
        filePaths.push(...collectSourceFilePaths(entryPath, listDirectory));
      }
    } else if (SOURCE_FILE_EXTENSIONS.has(path.extname(entry.name))) {
      filePaths.push(entryPath);
    }
  }

  return filePaths;
};

const collectAllSourceFilePaths = (
  directoryPaths: readonly string[],
  listDirectory: FormatCppEnvironment["listDirectory"],
): readonly string[] =>
  directoryPaths.flatMap((directoryPath) => collectSourceFilePaths(directoryPath, listDirectory)).toSorted();

const resolveClangFormatBinary = (
  readClangFormatVersion: FormatCppEnvironment["readClangFormatVersion"],
): ClangFormatBinary | null => {
  for (const binaryName of CANDIDATE_BINARY_NAMES) {
    const version = readClangFormatVersion(binaryName);

    if (version !== null && version.startsWith(EXPECTED_CLANG_FORMAT_VERSION_PREFIX)) {
      return { binaryName, version };
    }
  }

  return null;
};

const buildClangFormatArguments = (mode: FormatCppMode, filePaths: readonly string[]): readonly string[] => [
  ...(mode === "check" ? ["--dry-run", "--Werror"] : ["-i"]),
  "--style=file",
  ...filePaths,
];

const runFormatCpp = (mode: FormatCppMode, environment: FormatCppEnvironment, repositoryRoot: string): number => {
  const clangFormatBinary = resolveClangFormatBinary(environment.readClangFormatVersion);

  if (clangFormatBinary === null) {
    environment.report(
      `format:cpp requires a clang-format binary reporting version ${EXPECTED_CLANG_FORMAT_VERSION_PREFIX}x ` +
        `(tried: ${CANDIDATE_BINARY_NAMES.join(", ")}). Install clang-tools-18, or clang-format-18 directly, ` +
        "and retry.",
    );
    return FAILURE_EXIT_STATUS;
  }

  const directoryPaths = [
    path.join(repositoryRoot, "packages", "core", "src"),
    path.join(repositoryRoot, "packages", "core", "tests"),
  ];
  const filePaths = collectAllSourceFilePaths(directoryPaths, environment.listDirectory);

  environment.report(
    `format:cpp: running ${clangFormatBinary.binaryName} (${clangFormatBinary.version}) over ${filePaths.length} files`,
  );

  const exitCode = environment.runClangFormat(clangFormatBinary.binaryName, buildClangFormatArguments(mode, filePaths));

  return exitCode === SUCCESSFUL_EXIT_STATUS ? SUCCESSFUL_EXIT_STATUS : FAILURE_EXIT_STATUS;
};

export {
  buildClangFormatArguments,
  collectAllSourceFilePaths,
  EXPECTED_CLANG_FORMAT_VERSION_PREFIX,
  resolveClangFormatBinary,
  runFormatCpp,
};
