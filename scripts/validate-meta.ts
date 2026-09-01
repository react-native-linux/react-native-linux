import { fileURLToPath } from "node:url";
import path from "node:path";
import { readdirSync } from "node:fs";
import { spawnSync } from "node:child_process";

type CheckOutcome = "failed" | "passed" | "skipped";

interface CheckResult {
  readonly outcome: CheckOutcome;
  readonly toolName: string;
}

interface MetaCheck {
  readonly binaryName: string;
  readonly buildArguments: (shellScriptPaths: readonly string[]) => readonly string[] | null;
  readonly toolName: string;
}

const NO_SHELL_SCRIPTS_FOUND_LENGTH = 0;
const SUCCESSFUL_EXIT_STATUS = 0;
const FAILURE_EXIT_STATUS = 1;

const excludedDirectoryNames = new Set(["node_modules", ".git", "coverage"]);

const findShellScriptPaths = (directoryPath: string): string[] => {
  const shellScriptPaths: string[] = [];

  for (const directoryEntry of readdirSync(directoryPath, { withFileTypes: true })) {
    const entryPath = path.join(directoryPath, directoryEntry.name);
    const isExcludedDirectory = directoryEntry.isDirectory() && excludedDirectoryNames.has(directoryEntry.name);

    if (directoryEntry.isDirectory() && !isExcludedDirectory) {
      shellScriptPaths.push(...findShellScriptPaths(entryPath));
    } else if (directoryEntry.isFile() && path.extname(directoryEntry.name) === ".sh") {
      shellScriptPaths.push(entryPath);
    }
  }

  return shellScriptPaths;
};

const isBinaryOnPath = (binaryName: string): boolean => {
  const versionCheck = spawnSync(binaryName, ["--version"], { stdio: "ignore" });

  return !versionCheck.error && versionCheck.status === SUCCESSFUL_EXIT_STATUS;
};

const buildSkippedResult = (toolName: string, reason: string): CheckResult => {
  process.stdout.write(`skipping ${toolName}: ${reason}\n`);
  return { outcome: "skipped", toolName };
};

const runMetaCheck = (
  metaCheck: MetaCheck,
  shellScriptPaths: readonly string[],
  repositoryRootPath: string,
): CheckResult => {
  if (!isBinaryOnPath(metaCheck.binaryName)) {
    return buildSkippedResult(metaCheck.toolName, `"${metaCheck.binaryName}" was not found on PATH`);
  }

  const checkArguments = metaCheck.buildArguments(shellScriptPaths);
  if (checkArguments === null) {
    return buildSkippedResult(metaCheck.toolName, "no shell scripts found");
  }

  process.stdout.write(`running ${metaCheck.toolName}\n`);
  const checkRun = spawnSync(metaCheck.binaryName, [...checkArguments], {
    cwd: repositoryRootPath,
    stdio: "inherit",
  });

  return { outcome: checkRun.status === SUCCESSFUL_EXIT_STATUS ? "passed" : "failed", toolName: metaCheck.toolName };
};

const repositoryRootPath = fileURLToPath(new URL("..", import.meta.url));

const metaChecks: readonly MetaCheck[] = [
  {
    binaryName: "actionlint",
    buildArguments: () => [],
    toolName: "actionlint",
  },
  {
    binaryName: "shellcheck",
    buildArguments: (shellScriptPaths) =>
      shellScriptPaths.length > NO_SHELL_SCRIPTS_FOUND_LENGTH ? shellScriptPaths : null,
    toolName: "shellcheck",
  },
  {
    binaryName: "shfmt",
    buildArguments: (shellScriptPaths) =>
      shellScriptPaths.length > NO_SHELL_SCRIPTS_FOUND_LENGTH ? ["-d", ...shellScriptPaths] : null,
    toolName: "shfmt",
  },
  {
    binaryName: "typos",
    buildArguments: () => ["."],
    toolName: "typos",
  },
  {
    binaryName: "gitleaks",
    buildArguments: () => ["detect", "--source", ".", "--no-banner", "--redact"],
    toolName: "gitleaks",
  },
];

const shellScriptPaths = findShellScriptPaths(repositoryRootPath);
const checkResults = metaChecks.map((metaCheck) => runMetaCheck(metaCheck, shellScriptPaths, repositoryRootPath));

for (const checkResult of checkResults) {
  process.stdout.write(`${checkResult.toolName}: ${checkResult.outcome}\n`);
}

const hasFailedCheck = checkResults.some((checkResult) => checkResult.outcome === "failed");
process.exitCode = hasFailedCheck ? FAILURE_EXIT_STATUS : SUCCESSFUL_EXIT_STATUS;
