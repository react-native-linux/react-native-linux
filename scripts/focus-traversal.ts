import { LEDGER_PATH, findTraversalProblems } from "./e2e/focus-traversal.ts";
import { existsSync, readFileSync } from "node:fs";
import { stderr, stdout } from "node:process";

import { parseScenario } from "./e2e/scenario.ts";
import path from "node:path";

const FAILURE_EXIT_STATUS = 1;
const NO_PROBLEMS = 0;

const repositoryRoot = path.resolve(import.meta.dirname, "..");
const scenarioDirectory = path.join(repositoryRoot, "packages/core/e2e");

const readRepositoryFile = (relativePath: string): string =>
  readFileSync(path.join(repositoryRoot, relativePath), "utf8");

const readScenarioSteps = (scenario: string): readonly string[] | null => {
  const scenarioPath = path.join(scenarioDirectory, scenario);

  return existsSync(scenarioPath)
    ? parseScenario(JSON.parse(readFileSync(scenarioPath, "utf8")), scenario).steps
    : null;
};

const problems = findTraversalProblems(
  readRepositoryFile("packages/core/src/FabricHost.cpp"),
  JSON.parse(readRepositoryFile(LEDGER_PATH)),
  readScenarioSteps,
);

if (problems.length === NO_PROBLEMS) {
  stdout.write(`focus traversal: every registered component is ledgered\n`);
} else {
  for (const problem of problems) {
    stderr.write(`${problem}\n`);
  }

  process.exitCode = FAILURE_EXIT_STATUS;
}
