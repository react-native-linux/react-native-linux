import { env, stdout } from "node:process";
import { execFileSync, spawnSync } from "node:child_process";
import { existsSync, mkdirSync, readFileSync, rmSync } from "node:fs";
import path from "node:path";

const EXPORT_BUFFER_BYTES = 268_435_456;
const FAILURE_EXIT_STATUS = 1;
const FIRST_LINE_NUMBER = 1;
const FULL_COVERAGE_PERCENT = 100;
const HIT_VALUE_INDEX = 1;
const LINE_NUMBER_FIELD_INDEX = 0;
const NO_RECORDS = 0;
const PERCENT_DECIMALS = 2;
const PERCENT_SCALE = 100;
const SUCCESSFUL_EXIT_STATUS = 0;
const TAKEN_VALUE_INDEX = 3;
const UNCOVERED = "-";
const ZERO_HITS = 0;

const BRANCH_RECORD_PREFIX = "BRDA:";
const LINE_RECORD_PREFIX = "DA:";
const SOURCE_FILE_PREFIX = "SF:";
const END_OF_RECORD = "end_of_record";

const exclusionMarkerPattern = /\/\/\s*COV_EXCL:?(?<reason>.*)$/u;

const repositoryRoot = path.resolve(import.meta.dirname, "..");
const buildDirectory = path.join(repositoryRoot, "build", "test");
const testBinaryPath = path.join(buildDirectory, "bin", "rnl_core_tests");
const coverageDirectory = path.join(buildDirectory, "coverage");
const rawProfilePath = path.join(coverageDirectory, "rnl_core_tests.profraw");
const mergedProfilePath = path.join(coverageDirectory, "rnl_core_tests.profdata");

const scopedSourcePaths: readonly string[] = [
  "packages/core/src/AnimationFrameQueue.cpp",
  "packages/core/src/Clipboard.cpp",
  "packages/core/src/DimensionsSource.cpp",
  "packages/core/src/EditorModel.cpp",
  "packages/core/src/FocusModel.cpp",
  "packages/core/src/FrameClock.cpp",
  "packages/core/src/FrameTiming.cpp",
  "packages/core/src/ImageContent.cpp",
  "packages/core/src/InputPipeline.cpp",
  "packages/core/src/LineBoxMetrics.cpp",
  "packages/core/src/LinuxAnimationChoreographer.cpp",
  "packages/core/src/LinuxMountingManager.cpp",
  "packages/core/src/RetainedScene.cpp",
  "packages/core/src/ScrollEventCadence.cpp",
  "packages/core/src/ScrollPhysics.cpp",
  "packages/core/src/TextInputV3State.cpp",
  "packages/core/src/ToplevelState.cpp",
];

const coverageTool = env["LLVM_COV"] ?? "llvm-cov";
const profileTool = env["LLVM_PROFDATA"] ?? "llvm-profdata";

interface RecordCount {
  readonly covered: number;
  readonly total: number;
}

interface RecordFieldSelector {
  readonly prefix: string;
  readonly valueIndex: number;
}

const readExcludedLines = (absolutePath: string): Set<number> => {
  const excludedLines = new Set<number>();

  for (const [index, sourceLine] of readFileSync(absolutePath, "utf8").split("\n").entries()) {
    const marker = exclusionMarkerPattern.exec(sourceLine);

    if (marker !== null) {
      const reason = marker.groups?.reason ?? "";

      if (reason.trim() === "") {
        throw new Error(`${absolutePath}:${index + FIRST_LINE_NUMBER} has a COV_EXCL marker without a reason`);
      }

      excludedLines.add(index + FIRST_LINE_NUMBER);
    }
  }

  return excludedLines;
};

const collectRecordsByFile = (lcovText: string): Map<string, string[]> => {
  const recordsByFile = new Map<string, string[]>();
  let currentRecords: string[] | null = null;

  for (const lcovLine of lcovText.split("\n")) {
    if (lcovLine.startsWith(SOURCE_FILE_PREFIX)) {
      currentRecords = [];
      recordsByFile.set(lcovLine.slice(SOURCE_FILE_PREFIX.length), currentRecords);
    } else if (lcovLine === END_OF_RECORD) {
      currentRecords = null;
    } else {
      currentRecords?.push(lcovLine);
    }
  }

  return recordsByFile;
};

const countRecords = (
  records: readonly string[],
  excludedLines: ReadonlySet<number>,
  fieldSelector: RecordFieldSelector,
): RecordCount => {
  const values = records
    .filter((record) => record.startsWith(fieldSelector.prefix))
    .map((record) => record.slice(fieldSelector.prefix.length).split(","))
    .filter((fields) => !excludedLines.has(Number(fields[LINE_NUMBER_FIELD_INDEX])))
    .map((fields) => fields[fieldSelector.valueIndex] ?? UNCOVERED);

  return {
    covered: values.filter((value) => value !== UNCOVERED && Number(value) > ZERO_HITS).length,
    total: values.length,
  };
};

const formatPercent = (count: RecordCount): string =>
  count.total === NO_RECORDS
    ? FULL_COVERAGE_PERCENT.toFixed(PERCENT_DECIMALS)
    : ((count.covered / count.total) * PERCENT_SCALE).toFixed(PERCENT_DECIMALS);

const findRecords = (recordsByFile: ReadonlyMap<string, string[]>, scopedPath: string): readonly string[] | null => {
  const absolutePath = path.join(repositoryRoot, scopedPath);

  for (const [reportedPath, records] of recordsByFile) {
    if (path.resolve(buildDirectory, reportedPath) === absolutePath) {
      return records;
    }
  }

  return null;
};

const buildCoverageFailures = (scopedPath: string, lines: RecordCount, branches: RecordCount): readonly string[] => {
  const failures: string[] = [];

  if (lines.total === NO_RECORDS) {
    failures.push(`${scopedPath}: the lcov export carried no line records; check the llvm-cov version`);
  }

  if (lines.covered !== lines.total) {
    failures.push(`${scopedPath}: line coverage is ${formatPercent(lines)}%, the gate is ${FULL_COVERAGE_PERCENT}%`);
  }

  if (branches.covered !== branches.total) {
    failures.push(
      `${scopedPath}: branch coverage is ${formatPercent(branches)}%, the gate is ${FULL_COVERAGE_PERCENT}%`,
    );
  }

  return failures;
};

const gradeSource = (recordsByFile: ReadonlyMap<string, string[]>, scopedPath: string): readonly string[] => {
  const records = findRecords(recordsByFile, scopedPath);

  if (records === null) {
    return [`${scopedPath}: no coverage mapping in the profile; is it compiled into rnl_core_tests?`];
  }

  const excludedLines = readExcludedLines(path.join(repositoryRoot, scopedPath));
  const lines = countRecords(records, excludedLines, { prefix: LINE_RECORD_PREFIX, valueIndex: HIT_VALUE_INDEX });
  const branches = countRecords(records, excludedLines, {
    prefix: BRANCH_RECORD_PREFIX,
    valueIndex: TAKEN_VALUE_INDEX,
  });

  stdout.write(
    `${scopedPath}: lines ${formatPercent(lines)}% (${lines.covered}/${lines.total}), ` +
      `branches ${formatPercent(branches)}% (${branches.covered}/${branches.total})\n`,
  );

  return buildCoverageFailures(scopedPath, lines, branches);
};

const runTestBinary = (): void => {
  const testRun = spawnSync(testBinaryPath, [], {
    cwd: buildDirectory,
    env: { ...env, LLVM_PROFILE_FILE: rawProfilePath },
    stdio: "inherit",
  });

  if (testRun.status !== SUCCESSFUL_EXIT_STATUS) {
    throw new Error(`${testBinaryPath} exited with status ${String(testRun.status)}`);
  }
};

const exportLcov = (): string => {
  execFileSync(profileTool, ["merge", "-sparse", rawProfilePath, "-o", mergedProfilePath], { stdio: "inherit" });

  return execFileSync(
    coverageTool,
    ["export", testBinaryPath, `--instr-profile=${mergedProfilePath}`, "--format=lcov"],
    { encoding: "utf8", maxBuffer: EXPORT_BUFFER_BYTES },
  );
};

if (existsSync(testBinaryPath)) {
  rmSync(coverageDirectory, { force: true, recursive: true });
  mkdirSync(coverageDirectory, { recursive: true });
  runTestBinary();

  const recordsByFile = collectRecordsByFile(exportLcov());
  const failures = scopedSourcePaths.flatMap((scopedPath) => gradeSource(recordsByFile, scopedPath));

  for (const failure of failures) {
    stdout.write(`${failure}\n`);
  }

  process.exitCode = failures.length === NO_RECORDS ? SUCCESSFUL_EXIT_STATUS : FAILURE_EXIT_STATUS;
} else {
  stdout.write(`${testBinaryPath} is missing. Build it with "cmake --build build/test --target rnl_core_tests".\n`);
  process.exitCode = FAILURE_EXIT_STATUS;
}
