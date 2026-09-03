import { describe, expect, it } from "vitest";
import { describeFrameTiming, findFrameBudgetFailures, parseFrameLogSummary } from "./frame-log.ts";

const FRAME_LOG_PATH = "build/e2e/animated-frames/frames.jsonl";
const BOTH_FAILURES = 2;

const budget = { minFrames: 60, p95Ms: 16.7 };

const summaryLine = (fields: string): string => `{"summary":true,${fields}}`;

const healthySummary = summaryLine('"frames":240,"discarded":2,"p50Ns":16000000,"p95Ns":16600000,"maxNs":31000000');

describe("parseFrameLogSummary", () => {
  it("reads the summary line out of a log of frame records", () => {
    const frameLog = [
      '{"seq":1,"presentedNs":1000000000,"refreshNs":16666666,"flags":1}',
      '{"seq":2,"presentedNs":1016000000,"refreshNs":16666666,"flags":1,"frameNs":16000000}',
      healthySummary,
      "",
    ].join("\n");

    expect(parseFrameLogSummary(frameLog)).toEqual({
      discarded: 2,
      frames: 240,
      maximumNanoseconds: 31_000_000,
      medianNanoseconds: 16_000_000,
      percentile95Nanoseconds: 16_600_000,
      unsupported: false,
    });
  });

  it("reports the unsupported flag the window writes without a wp_presentation global", () => {
    const frameLog = `${summaryLine('"frames":0,"discarded":0,"p50Ns":0,"p95Ns":0,"maxNs":0,"unsupported":true')}\n`;

    expect(parseFrameLogSummary(frameLog)?.unsupported).toBe(true);
  });

  it("returns null for a log that has no summary line", () => {
    expect(parseFrameLogSummary('{"seq":1,"presentedNs":1,"refreshNs":0,"flags":0}\n')).toBeNull();
  });

  it("returns null for an empty log", () => {
    expect(parseFrameLogSummary("")).toBeNull();
  });

  it("returns null when the summary line is not a JSON object", () => {
    expect(parseFrameLogSummary('[{"summary":true,"frames":0}]')).toBeNull();
  });

  it("reads a missing percentile as zero rather than failing the parse", () => {
    expect(parseFrameLogSummary(summaryLine('"frames":10'))).toEqual({
      discarded: 0,
      frames: 10,
      maximumNanoseconds: 0,
      medianNanoseconds: 0,
      percentile95Nanoseconds: 0,
      unsupported: false,
    });
  });
});

describe("findFrameBudgetFailures", () => {
  it("reports nothing when the run met both halves of the budget", () => {
    expect(findFrameBudgetFailures(parseFrameLogSummary(healthySummary), budget, FRAME_LOG_PATH)).toEqual([]);
  });

  it("reports a log the window never summarised", () => {
    expect(findFrameBudgetFailures(null, budget, FRAME_LOG_PATH)).toEqual([
      `the window wrote no frame-timing summary to ${FRAME_LOG_PATH}`,
    ]);
  });

  it("reports a compositor that advertised no wp_presentation", () => {
    const summary = parseFrameLogSummary(
      summaryLine('"frames":0,"discarded":0,"p50Ns":0,"p95Ns":0,"maxNs":0,"unsupported":true'),
    );

    expect(findFrameBudgetFailures(summary, budget, FRAME_LOG_PATH)).toEqual([
      `the compositor advertised no wp_presentation, so ${FRAME_LOG_PATH} carries no frame timings`,
    ]);
  });

  it("reports a run too short for its percentile to mean anything", () => {
    const summary = parseFrameLogSummary(
      summaryLine('"frames":12,"discarded":0,"p50Ns":16000000,"p95Ns":16000000,"maxNs":16000000'),
    );

    expect(findFrameBudgetFailures(summary, budget, FRAME_LOG_PATH)).toEqual([
      "only 12 frames were presented, the budget needs at least 60",
    ]);
  });

  it("reports a p95 over the budget", () => {
    const summary = parseFrameLogSummary(
      summaryLine('"frames":240,"discarded":0,"p50Ns":16000000,"p95Ns":24000000,"maxNs":40000000'),
    );

    expect(findFrameBudgetFailures(summary, budget, FRAME_LOG_PATH)).toEqual([
      "p95 frame time is 24.00 ms, the budget is 16.7 ms",
    ]);
  });

  it("reports a short run and a slow one together", () => {
    const summary = parseFrameLogSummary(
      summaryLine('"frames":12,"discarded":0,"p50Ns":16000000,"p95Ns":24000000,"maxNs":40000000'),
    );

    expect(findFrameBudgetFailures(summary, budget, FRAME_LOG_PATH)).toHaveLength(BOTH_FAILURES);
  });
});

describe("describeFrameTiming", () => {
  it("names every number the summary carries", () => {
    const summary = parseFrameLogSummary(healthySummary);

    expect(summary === null ? "" : describeFrameTiming(summary)).toBe(
      "240 frames, 2 discarded, p50 16.00 ms, p95 16.60 ms, max 31.00 ms",
    );
  });
});
