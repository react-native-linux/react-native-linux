import { describe, expect, it } from "vitest";
import { findIdleGapFailures } from "./idle-gap.ts";
import { parseScenario } from "./scenario.ts";

/**
 * `minIdleGapMs` (#703): the scenario field and its grader. `scenario.spec.ts` already sits at the repository's
 * line-count ceiling. See *Frame clock* in docs/cpp-toolchain.md.
 */

const MIN_IDLE_GAP_MS = 2000;
const FIRST_FRAME_NS = 1_000_000_000;
const REFRESH_NS = 8_333_333;
const IDLE_SPAN_NS = 2_500_000_000;
const LONG_AFTER_NS = 4_000_000_000;
const SECOND_FRAME_NS = FIRST_FRAME_NS + REFRESH_NS;
const THIRD_FRAME_NS = SECOND_FRAME_NS + REFRESH_NS;

const validScenario = {
  bundle: "idle-wake.js",
  expect: ["idle-wake: animated 120 frames"],
  name: "idle-wake",
  ready: "idle-wake: committed surface 1",
  steps: ["sleep 2500", "click 200 140"],
};

const frameLine = (presentedNanoseconds: number): string =>
  `{"seq":1,"presentedNs":${String(presentedNanoseconds)},"refreshNs":8333333,"flags":1}`;

const idleLog = [
  frameLine(FIRST_FRAME_NS),
  '{"journal":true,"dirtyToPresentNs":11000000,"paintNs":3000000,"hang":false}',
  frameLine(SECOND_FRAME_NS),
  frameLine(SECOND_FRAME_NS + IDLE_SPAN_NS),
  frameLine(THIRD_FRAME_NS + IDLE_SPAN_NS),
  '{"summary":true,"frames":4,"discarded":0,"p50Ns":8333333,"p95Ns":8333333,"maxNs":8333333}',
].join("\n");

describe("parseScenario minIdleGapMs", () => {
  it("defaults to null when the scenario omits it", () => {
    expect(parseScenario(validScenario, "fixture.json").minIdleGapMs).toBeNull();
  });

  it("reads an idle gap", () => {
    expect(parseScenario({ ...validScenario, minIdleGapMs: MIN_IDLE_GAP_MS }, "fixture.json").minIdleGapMs).toBe(
      MIN_IDLE_GAP_MS,
    );
  });

  it("rejects an idle gap of zero", () => {
    expect(() => parseScenario({ ...validScenario, minIdleGapMs: 0 }, "fixture.json")).toThrow(
      'fixture.json: "minIdleGapMs" must be a positive number',
    );
  });
});

describe("findIdleGapFailures", () => {
  it("asks nothing when the scenario does not opt in", () => {
    expect(findIdleGapFailures("", null)).toEqual([]);
  });

  it("passes a log whose longest span between presented frames reaches the gap", () => {
    expect(findIdleGapFailures(idleLog, MIN_IDLE_GAP_MS)).toEqual([]);
  });

  it("reports a window that kept presenting", () => {
    const busyLog = [FIRST_FRAME_NS, SECOND_FRAME_NS, THIRD_FRAME_NS].map((frame) => frameLine(frame)).join("\n");

    expect(findIdleGapFailures(busyLog, MIN_IDLE_GAP_MS)).toEqual([
      "the longest span without a presented frame is 8.33 ms, the scenario needs at least 2000 ms",
    ]);
  });

  it("reports a log with fewer than two presented frames as no gap at all", () => {
    expect(findIdleGapFailures(frameLine(FIRST_FRAME_NS), MIN_IDLE_GAP_MS)).toEqual([
      "the longest span without a presented frame is 0.00 ms, the scenario needs at least 2000 ms",
    ]);
  });

  it("measures nothing rather than skipping a frame record it cannot read", () => {
    const corruptedLog = [
      frameLine(FIRST_FRAME_NS),
      '{"presentedNs":-1}',
      '{"presentedNs":',
      frameLine(LONG_AFTER_NS),
    ].join("\n");

    expect(findIdleGapFailures(corruptedLog, MIN_IDLE_GAP_MS)).toEqual([
      "the longest span without a presented frame is 0.00 ms, the scenario needs at least 2000 ms",
    ]);
  });
});
