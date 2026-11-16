import { describe, expect, it } from "vitest";
import { findOccludedFallbackFailures } from "./occluded-fallback.ts";
import { parseScenario } from "./scenario.ts";

/** #770: the `occludedFallback` scenario field and its grader. */

const ACTIVE_NS = 8_333_333;
const CAPPED_NS = 16_666_666;
const INACTIVE_NS = 50_000_000;
const OCCLUDED_NS = 66_666_664;

const fallbackLine = (nanoseconds: number, activity: string): string =>
  `{"fallbackNs":${String(nanoseconds)},"activity":"${activity}"}`;

const suspendedLog = [
  fallbackLine(ACTIVE_NS, "active"),
  '{"seq":1,"presentedNs":1000000000,"refreshNs":8333333,"flags":1}',
  fallbackLine(INACTIVE_NS, "inactive"),
  fallbackLine(OCCLUDED_NS, "occluded"),
  fallbackLine(ACTIVE_NS, "active"),
].join("\n");

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

  it("passes an interval that grows while occluded and shrinks after it", () => {
    expect(findOccludedFallbackFailures(suspendedLog, true)).toEqual([]);
  });

  it("reports a window that was never occluded, naming what the log records", () => {
    expect(findOccludedFallbackFailures(fallbackLine(ACTIVE_NS, "active"), true)).toEqual([
      "the fallback interval must grow while the window is occluded and shrink after it, the log records active 8333333 ns",
    ]);
  });
});

describe("findOccludedFallbackFailures without a stretch to show", () => {
  it.each([
    [
      "no record before the occluded one",
      [
        [OCCLUDED_NS, "occluded"],
        [ACTIVE_NS, "active"],
      ],
    ],
    [
      "no record after the occluded one",
      [
        [ACTIVE_NS, "active"],
        [OCCLUDED_NS, "occluded"],
      ],
    ],
    [
      "an occluded interval that did not grow",
      [
        [INACTIVE_NS, "inactive"],
        [CAPPED_NS, "occluded"],
        [ACTIVE_NS, "active"],
      ],
    ],
    [
      "an interval that did not shrink back",
      [
        [ACTIVE_NS, "active"],
        [OCCLUDED_NS, "occluded"],
        [OCCLUDED_NS, "active"],
      ],
    ],
  ] as const)("reports %s", (_description, records) => {
    const log = records.map(([nanoseconds, activity]) => fallbackLine(nanoseconds, activity)).join("\n");

    expect(findOccludedFallbackFailures(log, true)).not.toEqual([]);
  });
});

describe("findOccludedFallbackFailures on an unreadable log", () => {
  it("fails rather than skipping a record it cannot read", () => {
    expect(findOccludedFallbackFailures(`${suspendedLog}\n{"fallbackNs":12`, true)).toEqual([
      "the frame log carries a fallback record it cannot read",
    ]);
  });
});
