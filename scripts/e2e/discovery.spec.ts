import { describe, expect, it } from "vitest";
import {
  findScenarioSources,
  planRuns,
  planScenarioLabels,
  readRequestedScenarios,
  readScenarioRuns,
  resolveRepeatCount,
} from "./discovery.ts";

const LAST_SOURCE_INDEX = -1;
const NO_REPEAT = 1;
const TEN_REPEATS = 10;
const ONE_FRAME = 1;
const FIRST_PLANNED_RUN = 0;
const SECOND_PLANNED_RUN = 1;
const THIRD_PLANNED_RUN = 2;

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
      snapshotsDirectory: "/repo/packages/reanimated/e2e/snapshots",
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

  it("refuses a trailing --scenario rather than running the whole suite", () => {
    expect(() => readRequestedScenarios("/repo/packages", ["node", "e2e.ts", "--scenario"], environment)).toThrow(
      "--scenario needs a scenario name",
    );
  });

  it("refuses an empty --scenario name", () => {
    expect(() => readRequestedScenarios("/repo/packages", ["node", "e2e.ts", "--scenario", ""], environment)).toThrow(
      "--scenario needs a scenario name",
    );
  });
});

describe("resolveRepeatCount", () => {
  it("defaults to one run when RNL_E2E_REPEAT is unset", () => {
    expect(resolveRepeatCount({})).toBe(NO_REPEAT);
  });

  it("reads a positive integer from RNL_E2E_REPEAT", () => {
    expect(resolveRepeatCount({ RNL_E2E_REPEAT: "10" })).toBe(TEN_REPEATS);
  });

  it("rejects zero", () => {
    expect(() => resolveRepeatCount({ RNL_E2E_REPEAT: "0" })).toThrow(
      'RNL_E2E_REPEAT must be a positive integer, got "0"',
    );
  });

  it("rejects a negative count", () => {
    expect(() => resolveRepeatCount({ RNL_E2E_REPEAT: "-1" })).toThrow(
      'RNL_E2E_REPEAT must be a positive integer, got "-1"',
    );
  });

  it("rejects a non-integer count", () => {
    expect(() => resolveRepeatCount({ RNL_E2E_REPEAT: "3.5" })).toThrow(
      'RNL_E2E_REPEAT must be a positive integer, got "3.5"',
    );
  });

  it("rejects text that is not a number", () => {
    expect(() => resolveRepeatCount({ RNL_E2E_REPEAT: "ten" })).toThrow(
      'RNL_E2E_REPEAT must be a positive integer, got "ten"',
    );
  });

  it("rejects an explicitly empty value", () => {
    expect(() => resolveRepeatCount({ RNL_E2E_REPEAT: "" })).toThrow(
      'RNL_E2E_REPEAT must be a positive integer, got ""',
    );
  });

  it("rejects a key present but set to undefined, same as an empty value", () => {
    const emptyEnvironment: Readonly<Record<string, string | undefined>> = {};

    expect(() => resolveRepeatCount({ RNL_E2E_REPEAT: emptyEnvironment["RNL_E2E_REPEAT"] })).toThrow(
      'RNL_E2E_REPEAT must be a positive integer, got ""',
    );
  });
});

describe("planScenarioLabels", () => {
  it("reports the bare scenario name once when the scenario has no keyboard step", () => {
    expect(planScenarioLabels("pressable-click", false, TEN_REPEATS)).toEqual(["pressable-click"]);
  });

  it("reports the bare scenario name once when repeatCount is the default", () => {
    expect(planScenarioLabels("shadow-flicker", true, NO_REPEAT)).toEqual(["shadow-flicker"]);
  });

  it("reports one labelled run per repeat for a keyboard scenario", () => {
    expect(planScenarioLabels("shadow-flicker", true, TEN_REPEATS)).toEqual([
      "shadow-flicker (run 1/10)",
      "shadow-flicker (run 2/10)",
      "shadow-flicker (run 3/10)",
      "shadow-flicker (run 4/10)",
      "shadow-flicker (run 5/10)",
      "shadow-flicker (run 6/10)",
      "shadow-flicker (run 7/10)",
      "shadow-flicker (run 8/10)",
      "shadow-flicker (run 9/10)",
      "shadow-flicker (run 10/10)",
    ]);
  });
});

interface FixtureScenarioRun {
  readonly scenario: {
    readonly allowErrors: boolean;
    readonly automation: null;
    readonly bundle: string;
    readonly expect: readonly string[];
    readonly expectFailure: boolean;
    readonly frameBudget: null;
    readonly frames: number;
    readonly name: string;
    readonly ready: string;
    readonly screenshot: null;
    readonly steps: readonly string[];
  };
  readonly source: { readonly bundlesDirectory: string; readonly filePath: string; readonly goldensDirectory: string };
}

const scenarioRun = (name: string, steps: readonly string[]): FixtureScenarioRun => ({
  scenario: {
    allowErrors: false,
    automation: null,
    bundle: "bundle.js",
    expect: ["ready"],
    expectFailure: false,
    frameBudget: null,
    frames: ONE_FRAME,
    name,
    ready: "ready",
    screenshot: null,
    steps,
  },
  source: { bundlesDirectory: "", filePath: "", goldensDirectory: "" },
});

describe("planRuns", () => {
  it("plans one run per scenario when RNL_E2E_REPEAT is unset", () => {
    const runs = [scenarioRun("pressable-click", ["click 1 1"]), scenarioRun("shadow-flicker", ["key Tab press"])];

    expect(planRuns(runs, {}).map((planned) => planned.label)).toEqual(["pressable-click", "shadow-flicker"]);
  });

  it("repeats only the keyboard scenarios, and keeps the run each label points at", () => {
    const pressable = scenarioRun("pressable-click", ["click 1 1"]);
    const shadowFlicker = scenarioRun("shadow-flicker", ["key Tab press"]);

    const planned = planRuns([pressable, shadowFlicker], { RNL_E2E_REPEAT: "2" });

    expect(planned.map((entry) => entry.label)).toEqual([
      "pressable-click",
      "shadow-flicker (run 1/2)",
      "shadow-flicker (run 2/2)",
    ]);
    expect(planned[FIRST_PLANNED_RUN]?.run).toBe(pressable);
    expect(planned[SECOND_PLANNED_RUN]?.run).toBe(shadowFlicker);
    expect(planned[THIRD_PLANNED_RUN]?.run).toBe(shadowFlicker);
  });
});
