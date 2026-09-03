import { COMPONENT_SOURCES, enumerateComponentProps } from "./prop-coverage/enumerate-props.ts";
import type { ComponentProps, PropCoverageLedger } from "./prop-coverage/prop-coverage-types.ts";
import { argv, stderr, stdout } from "node:process";
import { findLedgerProblems, parseLedger } from "./prop-coverage/prop-coverage-ledger.ts";
import { readFileSync, readdirSync, writeFileSync } from "node:fs";

import path from "node:path";
import { renderPropCoverage } from "./prop-coverage/render-prop-coverage.ts";

const COMMAND_ARGUMENT_INDEX = 2;
const FAILURE_EXIT_STATUS = 1;
const NO_PROBLEMS = 0;

const LEDGER_PATH = "docs/prop-coverage.json";
const REPORT_PATH = "docs/prop-coverage.md";
const TESTS_DIRECTORY = "packages/core/tests";
const GOLDENS_DIRECTORY = "packages/core/goldens";

const repositoryRoot = path.resolve(import.meta.dirname, "..");

const readRepositoryFile = (relativePath: string): string =>
  readFileSync(path.join(repositoryRoot, relativePath), "utf8");

const readProofSources = (directory: string, extension: string): readonly string[] =>
  readdirSync(path.join(repositoryRoot, directory), { encoding: "utf8", recursive: true })
    .filter((entry) => entry.endsWith(extension))
    .map((entry) => readRepositoryFile(path.join(directory, entry)));

const buildProofCorpus = (): string =>
  [...readProofSources(TESTS_DIRECTORY, ".cpp"), ...readProofSources(GOLDENS_DIRECTORY, ".ts")].join("\n");

const loadCoverage = (): { components: readonly ComponentProps[]; ledger: PropCoverageLedger } => ({
  components: enumerateComponentProps(COMPONENT_SOURCES, readRepositoryFile),
  ledger: parseLedger(readRepositoryFile(LEDGER_PATH), LEDGER_PATH),
});

const runCheck = (): void => {
  const { components, ledger } = loadCoverage();
  const problems = [...findLedgerProblems(components, ledger, buildProofCorpus())];

  if (renderPropCoverage(components, ledger) !== readRepositoryFile(REPORT_PATH)) {
    problems.push(`${REPORT_PATH} is stale. Regenerate it with: pnpm prop-coverage:report`);
  }

  if (problems.length === NO_PROBLEMS) {
    stdout.write(`prop coverage: ${components.flatMap((component) => component.props).length} props, no drift\n`);

    return;
  }

  for (const problem of problems) {
    stderr.write(`${problem}\n`);
  }

  process.exitCode = FAILURE_EXIT_STATUS;
};

const runReport = (): void => {
  const { components, ledger } = loadCoverage();

  writeFileSync(path.join(repositoryRoot, REPORT_PATH), renderPropCoverage(components, ledger));
  stdout.write(`wrote ${REPORT_PATH}\n`);
};

const command = argv[COMMAND_ARGUMENT_INDEX];

if (command === "check") {
  runCheck();
} else if (command === "report") {
  runReport();
} else {
  stderr.write("prop-coverage: expected a command of check or report\n");
  process.exitCode = FAILURE_EXIT_STATUS;
}
