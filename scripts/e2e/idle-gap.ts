const NANOSECONDS_PER_MILLISECOND = 1_000_000;
const MILLISECOND_DECIMALS = 2;
const NO_GAP = 0;

/**
 * A per-frame `FrameTiming` record carries `presentedNs` as an integer; the journal's own records and the summaries do
 * not carry it at all, so a line that names it but does not match is a corrupted record.
 */
const PRESENTED_MARKER = '"presentedNs":';
const PRESENTED_PATTERN = /"presentedNs":(?<nanoseconds>\d+)[,}]/u;

const readPresentedNanoseconds = (line: string): number | null => {
  const presentedNanoseconds = Number(PRESENTED_PATTERN.exec(line)?.groups?.["nanoseconds"]);

  return Number.isSafeInteger(presentedNanoseconds) ? presentedNanoseconds : null;
};

/**
 * The longest span between two consecutive presented frames, in nanoseconds; zero for fewer than two. A record it
 * cannot read makes the whole measurement zero rather than being skipped, because skipping one would lengthen a span
 * and pass a scenario gating on an idle gap the window may never have had.
 */
const measureLongestIdleNanoseconds = (frameLogText: string): number => {
  const records = frameLogText
    .split("\n")
    .filter((line) => line.includes(PRESENTED_MARKER))
    .map((line) => readPresentedNanoseconds(line));
  let longest = NO_GAP;
  let previous: number | null = null;

  for (const presented of records) {
    if (presented === null) {
      return NO_GAP;
    }

    longest = previous === null ? longest : Math.max(longest, presented - previous);
    previous = presented;
  }

  return longest;
};

/**
 * The e2e half of #703's acceptance: an idle visible window stops presenting, so its frame log carries a span
 * without a presented frame at least `minIdleGapMs` long. `null` asks nothing.
 */
const findIdleGapFailures = (frameLogText: string, minIdleGapMs: number | null): readonly string[] => {
  if (minIdleGapMs === null) {
    return [];
  }

  const longestIdleNanoseconds = measureLongestIdleNanoseconds(frameLogText);

  return longestIdleNanoseconds >= minIdleGapMs * NANOSECONDS_PER_MILLISECOND
    ? []
    : [
        `the longest span without a presented frame is ` +
          `${(longestIdleNanoseconds / NANOSECONDS_PER_MILLISECOND).toFixed(MILLISECOND_DECIMALS)} ms, ` +
          `the scenario needs at least ${String(minIdleGapMs)} ms`,
      ];
};

export { findIdleGapFailures };
