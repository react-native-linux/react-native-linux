import { describe, expect, it } from "vitest";
import { findFrameBudgetFailures, parseFrameLogSummary } from "./frame-log.ts";
import { parseScenario } from "./scenario.ts";

/**
 * `frameBudget.maxHangs` (#345) and `frameBudget.p50Ms` (#20), split from `scenario.spec.ts` and `frame-log.spec.ts`
 * rather than added to them: those files already sit at the repository's line-count ceiling. See *Frame journal* and
 * *At 120 Hz* in docs/cpp-toolchain.md.
 */

const MINIMUM_FRAMES = 60;
const BUDGET_P95_MS = 16.7;
const MAX_HANGS = 0;
const BUDGET_P50_MS = 8.5;

const validScenario = {
  bundle: "pressable.js",
  expect: ["pressable: topClick on box at 200,140"],
  name: "pressable-click",
  ready: "pressable: committed surface 1",
  steps: ["sleep 500", "click 200 140"],
};

describe("parseScenario frameBudget maxHangs", () => {
  const frameBudget = { minFrames: MINIMUM_FRAMES, p95Ms: BUDGET_P95_MS };

  it("defaults to null when the scenario omits it", () => {
    expect(parseScenario({ ...validScenario, frameBudget }, "fixture.json").frameBudget?.maxHangs).toBeNull();
  });

  it("reads an explicit hang budget of zero", () => {
    expect(
      parseScenario({ ...validScenario, frameBudget: { ...frameBudget, maxHangs: MAX_HANGS } }, "fixture.json")
        .frameBudget,
    ).toEqual({ ...frameBudget, maxHangs: MAX_HANGS, maxJournalledFrames: null });
  });

  it("rejects a negative hang budget", () => {
    expect(() =>
      parseScenario({ ...validScenario, frameBudget: { ...frameBudget, maxHangs: -1 } }, "fixture.json"),
    ).toThrow('fixture.json: "frameBudget.maxHangs" must be a non-negative integer');
  });

  it("rejects a fractional hang budget", () => {
    expect(() =>
      parseScenario({ ...validScenario, frameBudget: { ...frameBudget, maxHangs: 1.5 } }, "fixture.json"),
    ).toThrow('fixture.json: "frameBudget.maxHangs" must be a non-negative integer');
  });
});

describe("parseScenario frameBudget p50Ms", () => {
  const frameBudget = { minFrames: MINIMUM_FRAMES, p95Ms: BUDGET_P95_MS };

  it("is absent when the scenario omits it", () => {
    expect(parseScenario({ ...validScenario, frameBudget }, "fixture.json").frameBudget).not.toHaveProperty("p50Ms");
  });

  it("reads a median budget", () => {
    expect(
      parseScenario({ ...validScenario, frameBudget: { ...frameBudget, p50Ms: BUDGET_P50_MS } }, "fixture.json")
        .frameBudget?.p50Ms,
    ).toBe(BUDGET_P50_MS);
  });

  it("rejects a median budget of zero", () => {
    expect(() =>
      parseScenario({ ...validScenario, frameBudget: { ...frameBudget, p50Ms: 0 } }, "fixture.json"),
    ).toThrow('fixture.json: "frameBudget.p50Ms" must be a positive number');
  });
});

describe("findFrameBudgetFailures p50Ms", () => {
  const budget = { maxHangs: null, maxJournalledFrames: null, minFrames: MINIMUM_FRAMES, p95Ms: 12.5 };
  const summary = parseFrameLogSummary(
    '{"summary":true,"frames":479,"discarded":0,"p50Ns":10390000,"p95Ns":11620000,"maxNs":43000000}',
  );
  const inputs = { frameLogPath: "frames.jsonl", journalSummary: null, summary };

  it("reports a median over its budget while the p95 is inside its own", () => {
    expect(findFrameBudgetFailures({ ...inputs, budget: { ...budget, p50Ms: BUDGET_P50_MS } })).toEqual([
      "p50 frame time is 10.39 ms, the budget is 8.5 ms",
    ]);
  });

  it("does not gate on the median when the scenario sets no p50Ms", () => {
    expect(findFrameBudgetFailures({ ...inputs, budget })).toEqual([]);
  });
});
