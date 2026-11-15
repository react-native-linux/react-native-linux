import { findFirstPresentedAfter, parsePresentationLog, summarizeFramePacing } from "./presentation.ts";
import type { TreeUsage } from "./processes.ts";

const NANOSECONDS_PER_SECOND = 1_000_000_000;
const NANOSECONDS_PER_MILLISECOND = 1_000_000;
const MILLISECONDS_PER_SECOND = 1000;
const KILOBYTES_PER_MEBIBYTE = 1024;
const MILLISECOND_DECIMALS = 2;
const WHOLE_DECIMALS = 0;

type FramePacing = ReturnType<typeof summarizeFramePacing>;

interface MeasuredPhase {
  readonly endNanoseconds: number;
  readonly name: string;
  readonly startNanoseconds: number;
  readonly usage: TreeUsage;
}

/** What one run of the app recorded; every timestamp is `CLOCK_MONOTONIC` nanoseconds. */
interface AppRun {
  readonly phases: readonly MeasuredPhase[];
  readonly readyNanoseconds: number;
  readonly spawnedNanoseconds: number;
  readonly ticksPerSecond: number;
  readonly trace: string;
}

interface PhaseResult {
  readonly durationNanoseconds: number;
  readonly name: string;
  readonly pacing: FramePacing;
  readonly usage: TreeUsage;
}

interface RunResult {
  readonly discarded: number;
  /** Spawn to the first frame the compositor presented, whatever it showed. */
  readonly firstFrameNanoseconds: number | null;
  /** Spawn to the first frame presented after the sidebar's thread list was in the DOM. */
  readonly firstUsableFrameNanoseconds: number | null;
  readonly phases: readonly PhaseResult[];
  readonly refreshHz: number;
  readonly ticksPerSecond: number;
}

/** Null when the trace has no `wp_presentation` on the monotonic clock: then no frame was measured. */
const summarizeRun = (run: AppRun, refreshHz: number): RunResult | null => {
  const log = parsePresentationLog(run.trace);

  if (log === null) {
    return null;
  }

  const sinceSpawn = (nanoseconds: number | null): number | null =>
    nanoseconds === null ? null : nanoseconds - run.spawnedNanoseconds;

  return {
    discarded: log.discarded,
    firstFrameNanoseconds: sinceSpawn(findFirstPresentedAfter(log, run.spawnedNanoseconds)),
    firstUsableFrameNanoseconds: sinceSpawn(findFirstPresentedAfter(log, run.readyNanoseconds)),
    phases: run.phases.map((phase) => ({
      durationNanoseconds: phase.endNanoseconds - phase.startNanoseconds,
      name: phase.name,
      pacing: summarizeFramePacing(log, phase, NANOSECONDS_PER_SECOND / refreshHz),
      usage: phase.usage,
    })),
    refreshHz,
    ticksPerSecond: run.ticksPerSecond,
  };
};

const formatMilliseconds = (nanoseconds: number, decimals: number): string =>
  (nanoseconds / NANOSECONDS_PER_MILLISECOND).toFixed(decimals);

const formatOptionalMilliseconds = (nanoseconds: number | null): string =>
  nanoseconds === null ? "never" : `${formatMilliseconds(nanoseconds, WHOLE_DECIMALS)} ms`;

const formatMebibytes = (kilobytes: number): string => (kilobytes / KILOBYTES_PER_MEBIBYTE).toFixed(WHOLE_DECIMALS);

/** Client memory counts the GPU process: it is part of the window, where the backend is not. */
const formatPhaseRow = (phase: PhaseResult, ticksPerSecond: number): string => {
  const cpuMilliseconds = (ticks: number): string =>
    ((ticks * MILLISECONDS_PER_SECOND) / ticksPerSecond).toFixed(WHOLE_DECIMALS);
  const { pacing, usage } = phase;
  const cells = [
    phase.name,
    formatMilliseconds(phase.durationNanoseconds, WHOLE_DECIMALS),
    String(pacing.frames),
    formatMilliseconds(pacing.medianNanoseconds, MILLISECOND_DECIMALS),
    formatMilliseconds(pacing.percentile95Nanoseconds, MILLISECOND_DECIMALS),
    formatMilliseconds(pacing.maximumNanoseconds, MILLISECOND_DECIMALS),
    String(pacing.lateFrames),
    cpuMilliseconds(usage.client.cpuTicks),
    cpuMilliseconds(usage.gpu.cpuTicks),
    cpuMilliseconds(usage.backend.cpuTicks),
    formatMebibytes(usage.client.rssKilobytes + usage.gpu.rssKilobytes),
    formatMebibytes(usage.client.pssKilobytes + usage.gpu.pssKilobytes),
    formatMebibytes(usage.backend.rssKilobytes),
  ];

  return `| ${cells.join(" | ")} |`;
};

/** A Markdown table, the shape docs/research/t3code-electron-baseline.md checks in. */
const formatReport = (result: RunResult): string =>
  [
    `Output ${String(result.refreshHz)} Hz. First frame ${formatOptionalMilliseconds(result.firstFrameNanoseconds)}, ` +
      `first usable frame ${formatOptionalMilliseconds(result.firstUsableFrameNanoseconds)}, ` +
      `${String(result.discarded)} frames discarded.`,
    "",
    "| Phase | Duration (ms) | Frames | p50 (ms) | p95 (ms) | Max (ms) | Late | Client CPU (ms) | " +
      "GPU process CPU (ms) | Backend CPU (ms) | Client RSS (MiB) | Client PSS (MiB) | Backend RSS (MiB) |",
    "| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |",
    ...result.phases.map((phase) => formatPhaseRow(phase, result.ticksPerSecond)),
    "",
  ].join("\n");

export { formatReport, summarizeRun };
export type { AppRun, MeasuredPhase };
