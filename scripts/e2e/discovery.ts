import { parseScenario } from "./scenario.ts";
import path from "node:path";

const SCENARIOS_DIRECTORY_NAME = "e2e";
const GOLDENS_DIRECTORY_NAME = "goldens";
const BUNDLES_DIRECTORY_NAME = "test-bundles";
const SCENARIO_FILE_SUFFIX = ".json";

/** Where a scenario came from, and the two directories of the package that ships it. */
interface ScenarioSource {
  readonly bundlesDirectory: string;
  readonly filePath: string;
  readonly goldensDirectory: string;
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

export { findScenarioSources, readScenarioRuns };
