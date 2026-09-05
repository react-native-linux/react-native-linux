import { describe, expect, it } from "vitest";
import { findScenarioSources, readRequestedScenarios, readScenarioRuns } from "./discovery.ts";

const LAST_SOURCE_INDEX = -1;
const EVERY_SCENARIO_COUNT = 3;

const entriesByDirectory: Readonly<Record<string, readonly string[]>> = {
  "/repo/packages": ["reanimated", "core", "cli"],
  "/repo/packages/core/e2e": ["pressable.json", "animated-frames.json", "goldens", "notes.md"],
  "/repo/packages/reanimated/e2e": ["smoke.json"],
};

const scenariosByPath: Readonly<Record<string, string>> = {
  "/repo/packages/core/e2e/animated-frames.json": "animated-frames",
  "/repo/packages/core/e2e/pressable.json": "pressable-click",
  "/repo/packages/reanimated/e2e/smoke.json": "reanimated-smoke",
};

const listEntries = (directoryPath: string): readonly string[] => entriesByDirectory[directoryPath] ?? [];

const readTextFile = (filePath: string): string =>
  JSON.stringify({
    bundle: "bundle.js",
    expect: ["ready"],
    name: scenariosByPath[filePath] ?? "unknown",
    ready: "ready",
    steps: ["sleep 1"],
  });

const environment = { listEntries, readTextFile };

describe("findScenarioSources", () => {
  it("finds the scenarios of every package, in package and file order", () => {
    expect(findScenarioSources("/repo/packages", listEntries).map((source) => source.filePath)).toEqual([
      "/repo/packages/core/e2e/animated-frames.json",
      "/repo/packages/core/e2e/pressable.json",
      "/repo/packages/reanimated/e2e/smoke.json",
    ]);
  });

  it("grades a scenario against the bundles and goldens of its own package", () => {
    expect(findScenarioSources("/repo/packages", listEntries).at(LAST_SOURCE_INDEX)).toEqual({
      bundlesDirectory: "/repo/packages/reanimated/test-bundles",
      filePath: "/repo/packages/reanimated/e2e/smoke.json",
      goldensDirectory: "/repo/packages/reanimated/e2e/goldens",
    });
  });

  it("finds nothing when no package ships a scenario", () => {
    expect(findScenarioSources("/repo/packages", () => [])).toEqual([]);
  });
});

describe("readScenarioRuns", () => {
  it("parses every discovered scenario next to its source", () => {
    expect(readScenarioRuns("/repo/packages", null, environment).map((run) => run.scenario.name)).toEqual([
      "animated-frames",
      "pressable-click",
      "reanimated-smoke",
    ]);
  });

  it("keeps only the requested scenario", () => {
    const runs = readScenarioRuns("/repo/packages", "reanimated-smoke", environment);

    expect(runs.map((run) => run.source.filePath)).toEqual(["/repo/packages/reanimated/e2e/smoke.json"]);
  });

  it("keeps nothing when the requested scenario does not exist", () => {
    expect(readScenarioRuns("/repo/packages", "missing", environment)).toEqual([]);
  });
});

describe("readRequestedScenarios", () => {
  it("runs every scenario when the command line names none", () => {
    const runs = readRequestedScenarios("/repo/packages", ["node", "e2e.ts"], environment);

    expect(runs.map((run) => run.scenario.name)).toEqual(["animated-frames", "pressable-click", "reanimated-smoke"]);
  });

  it("runs only the scenario --scenario names", () => {
    const runs = readRequestedScenarios(
      "/repo/packages",
      ["node", "e2e.ts", "--scenario", "pressable-click"],
      environment,
    );

    expect(runs.map((run) => run.scenario.name)).toEqual(["pressable-click"]);
  });

  it("runs every scenario when --scenario is the last argument", () => {
    const runs = readRequestedScenarios("/repo/packages", ["node", "e2e.ts", "--scenario"], environment);

    expect(runs).toHaveLength(EVERY_SCENARIO_COUNT);
  });
});
