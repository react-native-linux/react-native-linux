import { describe, expect, it } from "vitest";
import { findOccludedFallbackFailures } from "./occluded-fallback.ts";
import { parseScenario } from "./scenario.ts";

/** #770: the `occludedFallback` scenario field and its grader, which reads the activity sequence only. */

const fallbackLog = (...activities: readonly string[]): string =>
  activities.map((activity) => `{"fallbackNs":8333333,"activity":"${activity}"}`).join("\n");

describe("parseScenario occludedFallback", () => {
  it("reads the opt-in", () => {
    const scenario = {
      bundle: "app-state.js",
      expect: ["app-state: change 1 background"],
      name: "app-state",
      occludedFallback: true,
      ready: "app-state: committed surface 1",
      steps: ["sleep 2000"],
    };

    expect(parseScenario(scenario, "fixture.json").occludedFallback).toBe(true);
  });
});

describe("findOccludedFallbackFailures", () => {
  it("asks nothing when the scenario does not opt in", () => {
    expect(findOccludedFallbackFailures("", false)).toEqual([]);
  });

  it("passes a fallback that goes occluded and back, whatever the intervals", () => {
    const log = [
      fallbackLog("active", "inactive"),
      '{"seq":1,"presentedNs":1000000000,"refreshNs":8333333,"flags":1}',
      fallbackLog("occluded", "inactive"),
    ].join("\n");

    expect(findOccludedFallbackFailures(log, true)).toEqual([]);
  });

  it.each([
    ["never occluded", ["active", "inactive"]],
    ["occluded from the start", ["occluded", "active"]],
    ["never resumed", ["active", "occluded"]],
  ])("reports a fallback %s", (_description, activities) => {
    expect(findOccludedFallbackFailures(fallbackLog(...activities), true)).toEqual([
      `the fallback must go from another activity to occluded and back, the log records ${activities.join(" → ")}`,
    ]);
  });

  it("fails rather than skipping a record it cannot read", () => {
    expect(findOccludedFallbackFailures(`${fallbackLog("active")}\n{"fallbackNs":12`, true)).toEqual([
      "the frame log carries a fallback record it cannot read",
    ]);
  });
});
