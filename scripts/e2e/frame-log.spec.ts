import { describe, expect, it } from "vitest";
import {
  describeFrameJournal,
  describeFrameTiming,
  findFrameBudgetFailures,
  findInputTraceFailures,
  parseFrameJournalSummary,
  parseFrameLogSummary,
} from "./frame-log.ts";

import type { FrameBudget } from "./scenario.ts";

const FRAME_LOG_PATH = "build/e2e/animated-frames/frames.jsonl";
const BOTH_FAILURES = 2;

const budget: FrameBudget = { maxHangs: null, minFrames: 60, p95Ms: 17.5 };

const summaryLine = (fields: string): string => `{"summary":true,${fields}}`;
const journalSummaryLine = (fields: string): string => `{"journalSummary":true,${fields}}`;

const healthySummary = summaryLine('"frames":240,"discarded":2,"p50Ns":16000000,"p95Ns":16600000,"maxNs":31000000');
const healthyJournalSummary = journalSummaryLine(
  '"frames":238,"hangs":0,"p50Ns":11000000,"p95Ns":15000000,"maxNs":20000000',
);

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
});

describe("parseFrameLogSummary, on a summary it cannot trust", () => {
  it("returns null for a summary line that is not valid JSON at all", () => {
    expect(parseFrameLogSummary('{"summary":true,"frames":240')).toBeNull();
  });

  // Fail closed: a summary of defaulted zeroes would pass a p95 budget on a run that measured nothing.
  it.each(["discarded", "frames", "maxNs", "p50Ns", "p95Ns"])("returns null when %s is missing", (missingKey) => {
    const fields = ['"discarded":2', '"frames":240', '"maxNs":31000000', '"p50Ns":16000000', '"p95Ns":16600000']
      .filter((field) => !field.startsWith(`"${missingKey}"`))
      .join(",");

    expect(parseFrameLogSummary(summaryLine(fields))).toBeNull();
  });

  // Every field is a count or a duration, so Infinity (1e999), negatives and fractions are corruption.
  it.each(["1e999", "-1", "16.5"])("returns null when a field parses to %s", (corruptedValue) => {
    expect(
      parseFrameLogSummary(summaryLine(`"discarded":2,"frames":240,"maxNs":${corruptedValue},"p50Ns":0,"p95Ns":0`)),
    ).toBeNull();
  });
});

describe("parseFrameJournalSummary", () => {
  it("reads the journal summary line out of a log carrying both kinds of line", () => {
    const frameLog = [
      '{"seq":1,"presentedNs":1000000000,"refreshNs":16666666,"flags":1}',
      '{"journal":true,"dirtyToPresentNs":11000000,"paintNs":3000000,"hang":false}',
      healthySummary,
      healthyJournalSummary,
      "",
    ].join("\n");

    expect(parseFrameJournalSummary(frameLog)).toEqual({
      frames: 238,
      hangs: 0,
      maximumNanoseconds: 20_000_000,
      medianNanoseconds: 11_000_000,
      percentile95Nanoseconds: 15_000_000,
    });
  });

  it("returns null for a log that has no journal summary line", () => {
    expect(parseFrameJournalSummary(`${healthySummary}\n`)).toBeNull();
  });

  it("returns null for an empty log", () => {
    expect(parseFrameJournalSummary("")).toBeNull();
  });

  it("returns null when the journal summary line is not a JSON object", () => {
    expect(parseFrameJournalSummary('[{"journalSummary":true,"frames":0}]')).toBeNull();
  });
});

describe("parseFrameJournalSummary, on a summary it cannot trust", () => {
  it("returns null for a journal summary line that is not valid JSON at all", () => {
    expect(parseFrameJournalSummary('{"journalSummary":true,"hangs":0')).toBeNull();
  });

  // Fail closed: `hangs` defaulted to zero would pass a `maxHangs: 0` budget on a summary that never reported one.
  it.each(["frames", "hangs", "maxNs", "p50Ns", "p95Ns"])("returns null when %s is missing", (missingKey) => {
    const fields = ['"frames":238', '"hangs":0', '"maxNs":20000000', '"p50Ns":11000000', '"p95Ns":15000000']
      .filter((field) => !field.startsWith(`"${missingKey}"`))
      .join(",");

    expect(parseFrameJournalSummary(journalSummaryLine(fields))).toBeNull();
  });

  it("returns null when a field is not a number at all", () => {
    expect(
      parseFrameJournalSummary(journalSummaryLine('"frames":238,"hangs":"none","maxNs":0,"p50Ns":0,"p95Ns":0')),
    ).toBeNull();
  });

  // A negative hang count would otherwise pass a maxHangs of 0 on a line no window could have written.
  it.each(["1e999", "-1", "0.5"])("returns null when hangs parses to %s", (corruptedValue) => {
    expect(
      parseFrameJournalSummary(
        journalSummaryLine(`"frames":238,"hangs":${corruptedValue},"maxNs":0,"p50Ns":0,"p95Ns":0`),
      ),
    ).toBeNull();
  });
});

/** Every field `findFrameBudgetFailures` takes, defaulted to a run that met every half of the budget. */
const budgetInputs = (overrides: {
  readonly budget?: FrameBudget;
  readonly journalSummary?: ReturnType<typeof parseFrameJournalSummary>;
  readonly summary?: ReturnType<typeof parseFrameLogSummary>;
}): {
  readonly budget: FrameBudget;
  readonly frameLogPath: string;
  readonly journalSummary: ReturnType<typeof parseFrameJournalSummary>;
  readonly summary: ReturnType<typeof parseFrameLogSummary>;
} => ({
  budget: overrides.budget ?? budget,
  frameLogPath: FRAME_LOG_PATH,
  journalSummary:
    "journalSummary" in overrides ? overrides.journalSummary : parseFrameJournalSummary(healthyJournalSummary),
  summary: "summary" in overrides ? overrides.summary : parseFrameLogSummary(healthySummary),
});

describe("findFrameBudgetFailures", () => {
  it("reports nothing when the run met both halves of the budget", () => {
    expect(findFrameBudgetFailures(budgetInputs({}))).toEqual([]);
  });

  it("reports a log the window never summarised", () => {
    expect(findFrameBudgetFailures(budgetInputs({ journalSummary: null, summary: null }))).toEqual([
      `the window wrote no frame-timing summary to ${FRAME_LOG_PATH}`,
    ]);
  });

  it("reports a compositor that advertised no wp_presentation", () => {
    const summary = parseFrameLogSummary(
      summaryLine('"frames":0,"discarded":0,"p50Ns":0,"p95Ns":0,"maxNs":0,"unsupported":true'),
    );

    expect(findFrameBudgetFailures(budgetInputs({ journalSummary: null, summary }))).toEqual([
      `the compositor advertised no wp_presentation, so ${FRAME_LOG_PATH} carries no frame timings`,
    ]);
  });

  it("reports a run too short for its percentile to mean anything", () => {
    const summary = parseFrameLogSummary(
      summaryLine('"frames":12,"discarded":0,"p50Ns":16000000,"p95Ns":16000000,"maxNs":16000000'),
    );

    expect(findFrameBudgetFailures(budgetInputs({ journalSummary: null, summary }))).toEqual([
      "only 12 frames were presented, the budget needs at least 60",
    ]);
  });

  it("reports a p95 over the budget", () => {
    const summary = parseFrameLogSummary(
      summaryLine('"frames":240,"discarded":0,"p50Ns":16000000,"p95Ns":24000000,"maxNs":40000000'),
    );

    expect(findFrameBudgetFailures(budgetInputs({ journalSummary: null, summary }))).toEqual([
      "p95 frame time is 24.00 ms, the budget is 17.5 ms",
    ]);
  });

  it("reports a short run and a slow one together", () => {
    const summary = parseFrameLogSummary(
      summaryLine('"frames":12,"discarded":0,"p50Ns":16000000,"p95Ns":24000000,"maxNs":40000000'),
    );

    expect(findFrameBudgetFailures(budgetInputs({ journalSummary: null, summary }))).toHaveLength(BOTH_FAILURES);
  });
});

describe("findFrameBudgetFailures hang budget", () => {
  const hangBudget = { ...budget, maxHangs: 0 };

  it("does not gate on hangs when the scenario sets no maxHangs", () => {
    const journalSummary = parseFrameJournalSummary(
      journalSummaryLine('"frames":238,"hangs":5,"p50Ns":11000000,"p95Ns":15000000,"maxNs":20000000'),
    );

    expect(findFrameBudgetFailures(budgetInputs({ journalSummary }))).toEqual([]);
  });

  it("reports a run with no journal summary when the scenario gates on hangs", () => {
    expect(findFrameBudgetFailures(budgetInputs({ budget: hangBudget, journalSummary: null }))).toEqual([
      `the window wrote no frame-journal summary to ${FRAME_LOG_PATH}`,
    ]);
  });

  it("reports nothing when hangs are within the budget", () => {
    expect(findFrameBudgetFailures(budgetInputs({ budget: hangBudget }))).toEqual([]);
  });

  it("reports hangs over the budget", () => {
    const journalSummary = parseFrameJournalSummary(
      journalSummaryLine('"frames":238,"hangs":3,"p50Ns":11000000,"p95Ns":15000000,"maxNs":20000000'),
    );

    expect(findFrameBudgetFailures(budgetInputs({ budget: hangBudget, journalSummary }))).toEqual([
      "3 frames hung past the frame journal's thresholds, the budget allows at most 0",
    ]);
  });
});

describe("findInputTraceFailures", () => {
  const inputFrameLine = '{"journal":true,"dirtyToPresentNs":11000000,"inputEvents":2,"hang":false}';
  const noInputFrameLine = '{"journal":true,"dirtyToPresentNs":11000000,"hang":false}';

  const frameLogOf = (...journalLines: readonly string[]): string =>
    [...journalLines, healthySummary, healthyJournalSummary, ""].join("\n");

  it("asks nothing when the scenario does not opt in", () => {
    expect(findInputTraceFailures("", false, FRAME_LOG_PATH)).toEqual([]);
  });

  it("reports a log without a journal summary rather than grading a trace it cannot see", () => {
    expect(findInputTraceFailures(healthySummary, true, FRAME_LOG_PATH)).toEqual([
      `the window wrote no frame-journal summary to ${FRAME_LOG_PATH}`,
    ]);
  });

  it("reports a journal that closed no input-answering frame", () => {
    expect(findInputTraceFailures(frameLogOf(noInputFrameLine), true, FRAME_LOG_PATH)).toEqual([
      `no presented frame in ${FRAME_LOG_PATH} answered an injected input event in the frame journal's records`,
    ]);
  });

  it("counts a record that names the input its frame answered, skipping ones it cannot read", () => {
    const frameLog = frameLogOf('{"journal":true,"dirtyToPresentNs":11000000', inputFrameLine);

    expect(findInputTraceFailures(frameLog, true, FRAME_LOG_PATH)).toEqual([]);
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

describe("describeFrameJournal", () => {
  it("names every number the journal summary carries", () => {
    const summary = parseFrameJournalSummary(healthyJournalSummary);

    expect(summary === null ? "" : describeFrameJournal(summary)).toBe(
      "238 journalled frames, 0 hangs, dirty-to-present p50 11.00 ms, p95 15.00 ms, max 20.00 ms",
    );
  });
});
