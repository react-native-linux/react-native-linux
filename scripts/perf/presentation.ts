/*
 * The compositor's own record of when each frame reached the output: `wp_presentation_feedback.presented`, read out
 * of a client's `WAYLAND_DEBUG=client` trace. rnl_window reads the same events into its frame log, so a phase
 * summarised here is comparable with a FrameTiming summary (nearest-rank percentiles over consecutive intervals).
 */

const PRESENTED_PATTERN =
  /wp_presentation_feedback#\d+\.presented\((?<secondsHigh>\d+), (?<secondsLow>\d+), (?<nanoseconds>\d+),/u;
const DISCARDED_PATTERN = /wp_presentation_feedback#\d+\.discarded\(\)/u;
const CLOCK_PATTERN = /wp_presentation#\d+\.clock_id\((?<clockId>\d+)\)/u;
const SHM_BUFFER_PATTERN = /-> wl_shm_pool#\d+\.create_buffer\(/u;
const DMABUF_BUFFER_PATTERN = /-> zwp_linux_buffer_params_v1#\d+\.create(?:_immed)?\(/u;

/** `CLOCK_MONOTONIC`, the clock Node's `process.hrtime` reads, so phase bounds and frames share one timeline. */
const MONOTONIC_CLOCK_ID = 1;
/** `tv_sec_hi` counts units of 2^32 seconds. */
const SECONDS_HIGH_FACTOR = 4_294_967_296;
const NANOSECONDS_PER_SECOND = 1_000_000_000;
const MEDIAN_FRACTION = 0.5;
const PERCENTILE_95_FRACTION = 0.95;
const ONE_BASED_RANK = 1;
const NO_SAMPLES = 0;
const FIRST_INDEX = 0;

/** An interval this much longer than one refresh period means at least one deadline passed without a new frame. */
const LATE_INTERVAL_FACTOR = 1.5;

interface PresentationLog {
  readonly discarded: number;
  /** Buffers the client created in GPU memory and in shared CPU memory: which path its pixels took. */
  readonly dmabufBuffers: number;
  readonly presentedNanoseconds: readonly number[];
  readonly shmBuffers: number;
}

interface Phase {
  readonly endNanoseconds: number;
  readonly name: string;
  readonly startNanoseconds: number;
}

interface FramePacing {
  readonly frames: number;
  readonly lateFrames: number;
  readonly maximumNanoseconds: number;
  readonly medianNanoseconds: number;
  readonly percentile95Nanoseconds: number;
}

const readPresentedNanoseconds = (line: string): number | null => {
  const groups = PRESENTED_PATTERN.exec(line)?.groups ?? null;

  return groups === null
    ? null
    : (Number(groups["secondsHigh"]) * SECONDS_HIGH_FACTOR + Number(groups["secondsLow"])) * NANOSECONDS_PER_SECOND +
        Number(groups["nanoseconds"]);
};

/**
 * Null unless the client bound `wp_presentation` on the monotonic clock: without it the timestamps cannot be placed
 * against the phases, and a trace with no presentation global at all measured nothing.
 */
const parsePresentationLog = (trace: string): PresentationLog | null => {
  const lines = trace.split("\n");
  const clockId = lines.map((line) => CLOCK_PATTERN.exec(line)?.groups?.["clockId"] ?? null).find((id) => id !== null);

  if (Number(clockId) !== MONOTONIC_CLOCK_ID) {
    return null;
  }

  const count = (pattern: RegExp): number => lines.filter((line) => pattern.test(line)).length;

  return {
    discarded: count(DISCARDED_PATTERN),
    dmabufBuffers: count(DMABUF_BUFFER_PATTERN),
    presentedNanoseconds: lines
      .map((line) => readPresentedNanoseconds(line))
      .filter((nanoseconds) => nanoseconds !== null),
    shmBuffers: count(SHM_BUFFER_PATTERN),
  };
};

const percentile = (sortedValues: readonly number[], fraction: number): number =>
  sortedValues[Math.ceil(fraction * sortedValues.length) - ONE_BASED_RANK] ?? NO_SAMPLES;

const summarizeFramePacing = (log: PresentationLog, phase: Phase, refreshNanoseconds: number): FramePacing => {
  const presented = log.presentedNanoseconds.filter(
    (nanoseconds) => nanoseconds >= phase.startNanoseconds && nanoseconds <= phase.endNanoseconds,
  );
  const intervals = presented.flatMap((nanoseconds, index) => {
    const previous = presented[index - ONE_BASED_RANK] ?? null;

    return previous === null ? [] : [nanoseconds - previous];
  });
  const sorted = intervals.toSorted((left, right) => left - right);

  return {
    frames: presented.length,
    lateFrames: intervals.filter((interval) => interval > refreshNanoseconds * LATE_INTERVAL_FACTOR).length,
    maximumNanoseconds: sorted.at(-ONE_BASED_RANK) ?? NO_SAMPLES,
    medianNanoseconds: percentile(sorted, MEDIAN_FRACTION),
    percentile95Nanoseconds: percentile(sorted, PERCENTILE_95_FRACTION),
  };
};

const findFirstPresentedAfter = (log: PresentationLog, nanoseconds: number): number | null =>
  log.presentedNanoseconds.filter((presented) => presented >= nanoseconds).at(FIRST_INDEX) ?? null;

export { findFirstPresentedAfter, parsePresentationLog, summarizeFramePacing };
