import type { FrameBudget } from "./scenario.ts";

const NANOSECONDS_PER_MILLISECOND = 1_000_000;
const MILLISECOND_DECIMALS = 2;
const MISSING_NUMBER = 0;

/**
 * The last line `rnl_window --frame-log` writes. It is matched by its marker rather than by position so a run
 * that was killed mid-frame — leaving a truncated record behind — is reported as "no summary" instead of parsed
 * as one.
 */
const SUMMARY_MARKER = '"summary":true';

/**
 * The frame journal's own summary line (#345), beside `FrameTiming`'s: it is a separate JSON object rather than
 * extra keys on the same line, because the two are computed by independent pure classes on the C++ side —
 * `FrameJournal` knows nothing about `wp_presentation`, and combining their output into one struct would have
 * meant giving one of them a dependency on the other for no reason but the log format. See *Frame journal* in
 * docs/cpp-toolchain.md.
 */
const JOURNAL_SUMMARY_MARKER = '"journalSummary":true';

interface FrameLogSummary {
  readonly discarded: number;
  readonly frames: number;
  readonly maximumNanoseconds: number;
  readonly medianNanoseconds: number;
  readonly percentile95Nanoseconds: number;
  /** True when the compositor advertised no `wp_presentation` global at all, so nothing was measured. */
  readonly unsupported: boolean;
}

interface FrameJournalSummary {
  readonly frames: number;
  readonly hangs: number;
  readonly maximumNanoseconds: number;
  readonly medianNanoseconds: number;
  readonly percentile95Nanoseconds: number;
}

const isRecord = (value: unknown): value is Record<string, unknown> =>
  typeof value === "object" && value !== null && !Array.isArray(value);

const readNumber = (record: Record<string, unknown>, key: string): number => {
  const value = record[key];

  return typeof value === "number" ? value : MISSING_NUMBER;
};

const formatMilliseconds = (nanoseconds: number): string =>
  (nanoseconds / NANOSECONDS_PER_MILLISECOND).toFixed(MILLISECOND_DECIMALS);

const parseFrameLogSummary = (frameLogText: string): FrameLogSummary | null => {
  const summaryLine = frameLogText.split("\n").findLast((line) => line.includes(SUMMARY_MARKER)) ?? "";

  if (summaryLine === "") {
    return null;
  }

  const parsed: unknown = JSON.parse(summaryLine);

  if (!isRecord(parsed)) {
    return null;
  }

  return {
    discarded: readNumber(parsed, "discarded"),
    frames: readNumber(parsed, "frames"),
    maximumNanoseconds: readNumber(parsed, "maxNs"),
    medianNanoseconds: readNumber(parsed, "p50Ns"),
    percentile95Nanoseconds: readNumber(parsed, "p95Ns"),
    unsupported: parsed["unsupported"] === true,
  };
};

/**
 * The frame journal's own summary (#345): how many presented frames were closed against a `wp_presentation`
 * result, how many of those crossed a hang threshold, and the dirty-to-present percentiles over the ones that
 * did close — see `FrameJournal` on the C++ side for why an idle-boundary present produces no sample and is
 * therefore not counted here at all.
 */
const parseFrameJournalSummary = (frameLogText: string): FrameJournalSummary | null => {
  const summaryLine = frameLogText.split("\n").findLast((line) => line.includes(JOURNAL_SUMMARY_MARKER)) ?? "";

  if (summaryLine === "") {
    return null;
  }

  const parsed: unknown = JSON.parse(summaryLine);

  if (!isRecord(parsed)) {
    return null;
  }

  return {
    frames: readNumber(parsed, "frames"),
    hangs: readNumber(parsed, "hangs"),
    maximumNanoseconds: readNumber(parsed, "maxNs"),
    medianNanoseconds: readNumber(parsed, "p50Ns"),
    percentile95Nanoseconds: readNumber(parsed, "p95Ns"),
  };
};

/**
 * The frame-journal half of the budget (#345): `maxHangs` is optional, and `null` opts a scenario out rather
 * than defaulting to a number nobody measured. A scenario that does gate on it but whose run produced no journal
 * summary fails the same way a run with no `FrameTiming` summary does — a truncated log names its own reason
 * rather than being silently skipped.
 */
const findFrameHangFailures = (
  journalSummary: FrameJournalSummary | null,
  budget: FrameBudget,
  frameLogPath: string,
): readonly string[] => {
  if (budget.maxHangs === null) {
    return [];
  }

  if (journalSummary === null) {
    return [`the window wrote no frame-journal summary to ${frameLogPath}`];
  }

  if (journalSummary.hangs <= budget.maxHangs) {
    return [];
  }

  return [
    `${String(journalSummary.hangs)} frames hung past the frame journal's thresholds, the budget allows at most ` +
      `${String(budget.maxHangs)}`,
  ];
};

interface FrameBudgetGradeInputs {
  readonly budget: FrameBudget;
  readonly frameLogPath: string;
  readonly journalSummary: FrameJournalSummary | null;
  readonly summary: FrameLogSummary | null;
}

/**
 * Every reason the run missed its budget, so one CI failure reports both a short run and a slow one rather than
 * hiding the second behind the first.
 */
const findFrameBudgetFailures = (inputs: FrameBudgetGradeInputs): readonly string[] => {
  const { budget, frameLogPath, journalSummary, summary } = inputs;

  if (summary === null) {
    return [`the window wrote no frame-timing summary to ${frameLogPath}`];
  }

  if (summary.unsupported) {
    return [`the compositor advertised no wp_presentation, so ${frameLogPath} carries no frame timings`];
  }

  const budgetNanoseconds = budget.p95Ms * NANOSECONDS_PER_MILLISECOND;

  return [
    ...(summary.frames < budget.minFrames
      ? [`only ${String(summary.frames)} frames were presented, the budget needs at least ${String(budget.minFrames)}`]
      : []),
    ...(summary.percentile95Nanoseconds > budgetNanoseconds
      ? [
          `p95 frame time is ${formatMilliseconds(summary.percentile95Nanoseconds)} ms, ` +
            `the budget is ${String(budget.p95Ms)} ms`,
        ]
      : []),
    ...findFrameHangFailures(journalSummary, budget, frameLogPath),
  ];
};

const describeFrameTiming = (summary: FrameLogSummary): string =>
  `${String(summary.frames)} frames, ${String(summary.discarded)} discarded, ` +
  `p50 ${formatMilliseconds(summary.medianNanoseconds)} ms, ` +
  `p95 ${formatMilliseconds(summary.percentile95Nanoseconds)} ms, ` +
  `max ${formatMilliseconds(summary.maximumNanoseconds)} ms`;

/** Printed as a note on every run with a session, budget or not — see *Frame journal* in docs/cpp-toolchain.md. */
const describeFrameJournal = (summary: FrameJournalSummary): string =>
  `${String(summary.frames)} journalled frames, ${String(summary.hangs)} hangs, ` +
  `dirty-to-present p50 ${formatMilliseconds(summary.medianNanoseconds)} ms, ` +
  `p95 ${formatMilliseconds(summary.percentile95Nanoseconds)} ms, ` +
  `max ${formatMilliseconds(summary.maximumNanoseconds)} ms`;

export {
  describeFrameJournal,
  describeFrameTiming,
  findFrameBudgetFailures,
  parseFrameJournalSummary,
  parseFrameLogSummary,
};
