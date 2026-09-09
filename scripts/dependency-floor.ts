import {
  archDependencyList,
  computeDependencyFloor,
  debianDependencyLine,
} from "@react-native-linux/cli/dependency-floor.ts";
import { existsSync, readFileSync, writeFileSync } from "node:fs";
import { execFileSync } from "node:child_process";
import path from "node:path";

const FAILURE_EXIT_STATUS = 1;
const SUCCESS_EXIT_STATUS = 0;
const UPDATE_FLAG = "--update";
const COMMAND_ARGUMENTS_START = 2;
const LOCK_INDENTATION = 2;
const repositoryRoot = path.resolve(import.meta.dirname, "..");
const defaultBinaryPath = path.join(repositoryRoot, "build", "dev", "bin", "rnl_window");
const lockPath = path.join(repositoryRoot, "packages", "cli", "dependency-floor.lock.json");

interface ComputedFloor {
  readonly arch: readonly string[];
  readonly binary: string;
  readonly debian: string;
  readonly symbolFloor: {
    readonly cxxabi: string | null;
    readonly glibc: string | null;
    readonly glibcxx: string | null;
  };
}

const computedFloorFromBinary = (binaryPath: string): ComputedFloor => {
  const readelfArguments = ["-V", "-d"];
  const readelfOutput = execFileSync("readelf", [...readelfArguments, binaryPath], { encoding: "utf8" });
  const floor = computeDependencyFloor(readelfOutput);

  return {
    arch: archDependencyList(floor),
    binary: path.basename(binaryPath),
    debian: debianDependencyLine(floor),
    symbolFloor: floor.symbolFloor,
  };
};

const writeLock = (computed: ComputedFloor): number => {
  writeFileSync(lockPath, `${JSON.stringify(computed, null, LOCK_INDENTATION)}\n`);
  process.stdout.write(`wrote ${path.relative(repositoryRoot, lockPath)}\n`);

  return SUCCESS_EXIT_STATUS;
};

const compareLock = (computed: ComputedFloor): number => {
  if (!existsSync(lockPath)) {
    process.stderr.write(
      `${path.relative(repositoryRoot, lockPath)} does not exist yet; run with ${UPDATE_FLAG} to write it.\n`,
    );

    return FAILURE_EXIT_STATUS;
  }

  const locked: unknown = JSON.parse(readFileSync(lockPath, "utf8"));

  if (JSON.stringify(locked) === JSON.stringify(computed)) {
    process.stdout.write(`dependency floor matches ${path.relative(repositoryRoot, lockPath)}\n`);

    return SUCCESS_EXIT_STATUS;
  }

  process.stderr.write(
    `the dependency floor changed — a toolchain bump moved it. Review the diff and re-run with ${UPDATE_FLAG}:\n` +
      `locked:    ${JSON.stringify(locked)}\n` +
      `computed:  ${JSON.stringify(computed)}\n`,
  );

  return FAILURE_EXIT_STATUS;
};

const run = (): number => {
  const scriptArguments = process.argv.slice(COMMAND_ARGUMENTS_START);
  const updateRequested = scriptArguments.includes(UPDATE_FLAG);
  const firstArgumentIndex = 0;
  const binaryPath =
    scriptArguments.filter((argument) => argument !== UPDATE_FLAG)[firstArgumentIndex] ?? defaultBinaryPath;

  if (!existsSync(binaryPath)) {
    process.stderr.write(`${binaryPath} is missing; build it first — the floor is read off the ELF, not guessed.\n`);

    return FAILURE_EXIT_STATUS;
  }

  const computed = computedFloorFromBinary(binaryPath);

  return updateRequested ? writeLock(computed) : compareLock(computed);
};

process.exitCode = run();
