import { describe, expect, it } from "vitest";
import { findOccludedFallbackFailures } from "./occluded-fallback.ts";
import { parseScenario } from "./scenario.ts";

/** #770: the `occludedFallback` scenario field and its grader. */

const OCCLUDED_WAIT_NS = 400_000_000;
const PROMPT_NS = 100_000_000;
const LATE_NS = 400_000_000;
const STEP_NS = 50_000_000;
const START_NS = 0;

const fallbackLine = (activity: string, atNanoseconds: number, intervalNanoseconds = STEP_NS): string =>
  `{"fallbackNs":${String(intervalNanoseconds)},"activity":"${activity}","atNs":${String(atNanoseconds)}}`;

const resumeLog = (resumedAtNanoseconds: number): string =>
  [
    fallbackLine("active", START_NS),
    '{"seq":1,"presentedNs":1000000000,"refreshNs":8333333,"flags":1}',
    fallbackLine("occluded", STEP_NS, OCCLUDED_WAIT_NS),
    fallbackLine("inactive", STEP_NS + resumedAtNanoseconds),
  ].join("\n");

const sequenceLog = (...activities: readonly string[]): string =>
  activities.map((activity, index) => fallbackLine(activity, index * STEP_NS)).join("\n");

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

  it("passes a fallback that goes occluded and comes back well inside the occluded wait", () => {
    expect(findOccludedFallbackFailures(resumeLog(PROMPT_NS), true)).toEqual([]);
  });

  it("reports a resume that waited out the occluded interval", () => {
    expect(findOccludedFallbackFailures(resumeLog(LATE_NS), true)).toEqual([
      "the window resumed 400 ms after it went occluded, not inside half of its 400 ms occluded wait",
    ]);
  });

  it.each([
    ["never occluded", ["active", "inactive"]],
    ["occluded from the start", ["occluded", "active"]],
    ["never resumed", ["active", "occluded"]],
  ])("reports a fallback %s", (_description, activities) => {
    expect(findOccludedFallbackFailures(sequenceLog(...activities), true)).toEqual([
      `the fallback must go from another activity to occluded and back, the log records ${activities.join(" → ")}`,
    ]);
  });

  it("fails rather than skipping a record it cannot read", () => {
    expect(findOccludedFallbackFailures(`${sequenceLog("active")}\n{"fallbackNs":12`, true)).toEqual([
      "the frame log carries a fallback record it cannot read",
    ]);
  });
});
