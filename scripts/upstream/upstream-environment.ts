import type { CommandOutput, UpstreamEnvironment } from "./upstream-types.ts";

import { cpSync, existsSync, mkdirSync, mkdtempSync, readFileSync, readdirSync, rmSync, writeFileSync } from "node:fs";
import { createHash } from "node:crypto";
import path from "node:path";
import { spawnSync } from "node:child_process";
import { tmpdir } from "node:os";

const GIT_DIRECTORY_NAME = ".git";
const GIT_DIRECTORY_PREFIX = ".git/";
const TEMPORARY_DIRECTORY_PREFIX = "rnl-upstream-";
const MAXIMUM_GIT_OUTPUT_BYTES = 268_435_456;

const toPosixPath = (filePath: string): string => filePath.split(path.sep).join("/");

const runGit = (commandArguments: readonly string[], workingDirectory: string): CommandOutput => {
  const result = spawnSync("git", [...commandArguments], {
    cwd: workingDirectory,
    encoding: "utf8",
    maxBuffer: MAXIMUM_GIT_OUTPUT_BYTES,
  });

  return { exitCode: result.status, standardError: result.stderr, standardOutput: result.stdout };
};

const listFiles = (directoryPath: string): readonly string[] => {
  if (!existsSync(directoryPath)) {
    return [];
  }

  return readdirSync(directoryPath, { recursive: true, withFileTypes: true })
    .filter((entry) => entry.isFile())
    .map((entry) => toPosixPath(path.relative(directoryPath, path.join(entry.parentPath, entry.name))))
    .filter((relativePath) => !relativePath.startsWith(GIT_DIRECTORY_PREFIX))
    .toSorted();
};

const listDirectories = (directoryPath: string): readonly string[] => {
  if (!existsSync(directoryPath)) {
    return [];
  }

  return readdirSync(directoryPath, { withFileTypes: true })
    .filter((entry) => entry.isDirectory())
    .map((entry) => entry.name);
};

const createUpstreamEnvironment = (): UpstreamEnvironment => ({
  applyPatch: (patchFilePath, workingDirectory) => runGit(["apply", "--3way", "-p1", patchFilePath], workingDirectory),
  copyDirectory: (sourceDirectory, targetDirectory) => {
    cpSync(sourceDirectory, targetDirectory, {
      filter: (sourcePath) => path.basename(sourcePath) !== GIT_DIRECTORY_NAME,
      recursive: true,
    });
  },
  createTemporaryDirectory: () => mkdtempSync(path.join(tmpdir(), TEMPORARY_DIRECTORY_PREFIX)),
  hashFile: (filePath) => createHash("sha256").update(readFileSync(filePath)).digest("hex"),
  listDirectories,
  listFiles,
  pathExists: existsSync,
  readTextFile: (filePath) => readFileSync(filePath, "utf8"),
  removePath: (targetPath) => {
    rmSync(targetPath, { force: true, recursive: true });
  },
  runGit,
  writeTextFile: (filePath, contents) => {
    mkdirSync(path.dirname(filePath), { recursive: true });
    writeFileSync(filePath, contents);
  },
});

export { createUpstreamEnvironment };
