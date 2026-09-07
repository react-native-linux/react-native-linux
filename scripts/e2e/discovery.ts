import { hasKeyboardSteps } from "./keyboard-focus.ts";
import { parseScenario } from "./scenario.ts";
import path from "node:path";

const SCENARIOS_DIRECTORY_NAME = "e2e";
const GOLDENS_DIRECTORY_NAME = "goldens";
const SNAPSHOTS_DIRECTORY_NAME = "snapshots";
const BUNDLES_DIRECTORY_NAME = "test-bundles";
const SCENARIO_FILE_SUFFIX = ".json";
const SCENARIO_FLAG = "--scenario";
const NOT_FOUND_INDEX = -1;
const NEXT_ARGUMENT = 1;
const REPEAT_ENV_NAME = "RNL_E2E_REPEAT";
const DEFAULT_REPEAT_COUNT = 1;
const MINIMUM_REPEAT_COUNT = 1;
const FIRST_ATTEMPT = 1;

/**
 * Where a scenario came from, and the three directories of the package that ships it. `goldens` holds pictures
 * and `snapshots` holds serialised trees; they are apart because a picture is blessed from an artifact by
 * copying a file a reviewer cannot read, and a tree snapshot is reviewed in the pull request diff like code.
 */
interface ScenarioSource {
  readonly bundlesDirectory: string;
  readonly filePath: string;
  readonly goldensDirectory: string;
  readonly snapshotsDirectory: string;
}

interface ScenarioRun {
  readonly scenario: ReturnType<typeof parseScenario>;
  readonly source: ScenarioSource;
}

interface DiscoveryEnvironment {
  readonly listEntries: (directoryPath: string) => readonly string[];
  readonly readTextFile: (filePath: string) => string;
}

const findPackageScenarioSources = (
  packageDirectory: string,
  listEntries: DiscoveryEnvironment["listEntries"],
): readonly ScenarioSource[] => {
  const scenariosDirectory = path.join(packageDirectory, SCENARIOS_DIRECTORY_NAME);

  return listEntries(scenariosDirectory)
    .filter((entryName) => entryName.endsWith(SCENARIO_FILE_SUFFIX))
    .toSorted()
    .map((fileName) => ({
      bundlesDirectory: path.join(packageDirectory, BUNDLES_DIRECTORY_NAME),
      filePath: path.join(scenariosDirectory, fileName),
      goldensDirectory: path.join(scenariosDirectory, GOLDENS_DIRECTORY_NAME),
      snapshotsDirectory: path.join(scenariosDirectory, SNAPSHOTS_DIRECTORY_NAME),
    }));
};

/**
 * Conformance is per package, not per platform: every `packages/<lib>/e2e/*.json` is a scenario, graded against
 * the bundles and goldens of the package that ships it. `listEntries` answers with an empty list for a directory
 * that does not exist, so a package without scenarios contributes nothing.
 */
const findScenarioSources = (
  packagesDirectory: string,
  listEntries: DiscoveryEnvironment["listEntries"],
): readonly ScenarioSource[] =>
  listEntries(packagesDirectory)
    .toSorted()
    .flatMap((packageName) => findPackageScenarioSources(path.join(packagesDirectory, packageName), listEntries));

const readScenarioRuns = (
  packagesDirectory: string,
  requestedName: string | null,
  environment: DiscoveryEnvironment,
): readonly ScenarioRun[] =>
  findScenarioSources(packagesDirectory, environment.listEntries)
    .map((source) => {
      const parsed: unknown = JSON.parse(environment.readTextFile(source.filePath));

      return { scenario: parseScenario(parsed, source.filePath), source };
    })
    .filter((run) => requestedName === null || run.scenario.name === requestedName);

/**
 * The scenarios one `pnpm e2e` runs: every package's, or the single one `--scenario <name>` names. A trailing
 * `--scenario` with nothing after it is an argument error rather than "all of them", because the whole suite is
 * the most expensive thing a mistyped targeted run could do.
 */
const readRequestedScenarios = (
  packagesDirectory: string,
  commandArguments: readonly string[],
  environment: DiscoveryEnvironment,
): readonly ScenarioRun[] => {
  const flagIndex = commandArguments.indexOf(SCENARIO_FLAG);

  if (flagIndex === NOT_FOUND_INDEX) {
    return readScenarioRuns(packagesDirectory, null, environment);
  }

  const requestedName = commandArguments[flagIndex + NEXT_ARGUMENT] ?? "";

  if (requestedName === "") {
    throw new Error(`${SCENARIO_FLAG} needs a scenario name`);
  }

  return readScenarioRuns(packagesDirectory, requestedName, environment);
};

/**
 * How many times a keyboard scenario's acceptance run repeats, per the "ten green runs" acceptance of #304:
 * `RNL_E2E_REPEAT` unset or absent is one run, unchanged from today's CI. A scenario with no keyboard step never
 * repeats regardless of this value, because repeating it proves nothing about the keyboard-focus wait.
 */
const resolveRepeatCount = (environmentVariables: Readonly<Record<string, string | undefined>>): number => {
  if (!(REPEAT_ENV_NAME in environmentVariables)) {
    return DEFAULT_REPEAT_COUNT;
  }

  const raw = environmentVariables[REPEAT_ENV_NAME] ?? "";
  const parsed = Number(raw);

  if (!Number.isInteger(parsed) || parsed < MINIMUM_REPEAT_COUNT) {
    throw new Error(`${REPEAT_ENV_NAME} must be a positive integer, got "${raw}"`);
  }

  return parsed;
};

/**
 * `<name>` for the first attempt, `<name>#2`, `<name>#3`... after — a bug filed against #304 itself found that
 * `RNL_E2E_REPEAT`'s attempts all shared one artifact directory, so a later pass silently overwrote a failing
 * attempt's trace. This key is both the artifact directory name and the report line, so the two can never drift
 * apart again: whichever attempt fails, its own trace, screenshot and frame log survive under its own key.
 */
const describeAttemptKey = (scenarioName: string, attempt: number): string =>
  attempt === FIRST_ATTEMPT ? scenarioName : `${scenarioName}#${String(attempt)}`;

/**
 * The attempt keys for one scenario's runs — the "ten green runs" acceptance of #304. `needsRepeat` is false for
 * a scenario with no keyboard step, which never repeats regardless of `repeatCount`: repeating it proves nothing
 * about the keyboard-focus wait this issue adds. `repeatCount` of one produces the bare name, unchanged from
 * before #304.
 */
const planAttemptKeys = (scenarioName: string, needsRepeat: boolean, repeatCount: number): readonly string[] => {
  const iterations = needsRepeat ? repeatCount : DEFAULT_REPEAT_COUNT;

  return Array.from({ length: iterations }, (_unused, index) =>
    describeAttemptKey(scenarioName, index + FIRST_ATTEMPT),
  );
};

interface PlannedRun {
  readonly attemptKey: string;
  readonly run: ScenarioRun;
}

/**
 * Every run `scripts/e2e.ts` drives, each keyed for its own artifact directory and report line — the "ten green
 * runs" plan of #304. `environmentVariables` is read here, rather than by the caller, so `resolveRepeatCount`
 * stays this file's concern alone.
 */
const planRuns = (
  runs: readonly ScenarioRun[],
  environmentVariables: Readonly<Record<string, string | undefined>>,
): readonly PlannedRun[] => {
  const repeatCount = resolveRepeatCount(environmentVariables);
  const planned = runs.flatMap((run) =>
    planAttemptKeys(run.scenario.name, hasKeyboardSteps(run.scenario.steps), repeatCount).map((attemptKey) => ({
      attemptKey,
      run,
    })),
  );
  const seen = new Set<string>();

  /*
   * A scenario is free to be named "shadow-flicker#2", and then its first attempt would share a key — and an
   * artifact directory — with the second attempt of "shadow-flicker". Two runs writing one directory is the
   * overwrite the keys exist to prevent, so a collision is refused before anything runs.
   */
  for (const { attemptKey } of planned) {
    if (seen.has(attemptKey)) {
      throw new Error(`two planned runs share the attempt key "${attemptKey}"; rename one scenario`);
    }

    seen.add(attemptKey);
  }

  return planned;
};

export { isKeyboardFocused } from "./keyboard-focus.ts";
export { findScenarioSources, planAttemptKeys, planRuns, readRequestedScenarios, readScenarioRuns, resolveRepeatCount };
