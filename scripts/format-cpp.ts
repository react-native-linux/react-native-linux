import path from "node:path";
import { readdirSync } from "node:fs";
import { runFormatCpp } from "./format-cpp/run-format-cpp.ts";
import { spawnSync } from "node:child_process";

const SUCCESSFUL_EXIT_STATUS = 0;
const UNKNOWN_FAILURE_EXIT_STATUS = 1;

const scriptDirectory = import.meta.dirname;
const repositoryRoot = path.resolve(scriptDirectory, "..");
const mode = process.argv.includes("--check") ? "check" : "write";

const versionPattern = /clang-format version (?<version>\d+\.\d+\.\d+)/u;

const exitCode = runFormatCpp(
  mode,
  {
    listDirectory: (directoryPath) =>
      readdirSync(directoryPath, { withFileTypes: true }).map((entry) => ({
        isDirectory: entry.isDirectory(),
        name: entry.name,
      })),
    readClangFormatVersion: (binaryName) => {
      const versionCheck = spawnSync(binaryName, ["--version"], { encoding: "utf8" });

      if (versionCheck.error || versionCheck.status !== SUCCESSFUL_EXIT_STATUS) {
        return null;
      }

      return versionCheck.stdout.match(versionPattern)?.groups?.["version"] ?? null;
    },
    report: (message) => {
      process.stdout.write(`${message}\n`);
    },
    runClangFormat: (binaryName, args) =>
      spawnSync(binaryName, args, { stdio: "inherit" }).status ?? UNKNOWN_FAILURE_EXIT_STATUS,
  },
  repositoryRoot,
);

process.exitCode = exitCode;
