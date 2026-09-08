import { describe, expect, it } from "vitest";
import { parseScenario } from "./scenario.ts";

/**
 * `frameBudget.maxHangs` (#345), split from `scenario.spec.ts` rather than added to it: that file already sits
 * at the repository's line-count ceiling. See *Frame journal* in docs/cpp-toolchain.md.
 */

const MINIMUM_FRAMES = 60;
const BUDGET_P95_MS = 16.7;
const MAX_HANGS = 0;

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
    ).toEqual({ ...frameBudget, maxHangs: MAX_HANGS });
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
