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

interface FrameLogSummary {
  readonly discarded: number;
  readonly frames: number;
  readonly maximumNanoseconds: number;
  readonly medianNanoseconds: number;
  readonly percentile95Nanoseconds: number;
  /** True when the compositor advertised no `wp_presentation` global at all, so nothing was measured. */
  readonly unsupported: boolean;
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
 * Every reason the run missed its budget, so one CI failure reports both a short run and a slow one rather than
 * hiding the second behind the first.
 */
const findFrameBudgetFailures = (
  summary: FrameLogSummary | null,
  budget: FrameBudget,
  frameLogPath: string,
): readonly string[] => {
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
  ];
};

const describeFrameTiming = (summary: FrameLogSummary): string =>
  `${String(summary.frames)} frames, ${String(summary.discarded)} discarded, ` +
  `p50 ${formatMilliseconds(summary.medianNanoseconds)} ms, ` +
  `p95 ${formatMilliseconds(summary.percentile95Nanoseconds)} ms, ` +
  `max ${formatMilliseconds(summary.maximumNanoseconds)} ms`;

export { describeFrameTiming, findFrameBudgetFailures, parseFrameLogSummary };
