import { findFirstPresentedAfter, parsePresentationLog, summarizeFramePacing } from "./presentation.ts";
import type { TreeUsage } from "./processes.ts";

const NANOSECONDS_PER_SECOND = 1_000_000_000;
const NANOSECONDS_PER_MILLISECOND = 1_000_000;
const MILLISECONDS_PER_SECOND = 1000;
const KILOBYTES_PER_MEBIBYTE = 1024;
const MILLISECOND_DECIMALS = 2;
const WHOLE_DECIMALS = 0;
const GPU_FEATURES = ["gpu_compositing", "rasterization"];

type FramePacing = ReturnType<typeof summarizeFramePacing>;

const isRecord = (value: unknown): value is Record<string, unknown> =>
  typeof value === "object" && value !== null && !Array.isArray(value);

interface MeasuredPhase {
  readonly compositorUsage: TreeUsage;
  readonly endNanoseconds: number;
  readonly name: string;
  readonly startNanoseconds: number;
  readonly usage: TreeUsage;
}

/** What one run of the app recorded; every timestamp is `CLOCK_MONOTONIC` nanoseconds. */
interface AppRun {
  /** The browser's `SystemInfo.getInfo` result, which says how Chromium chose to draw. */
  readonly gpuInfo: unknown;
  readonly phases: readonly MeasuredPhase[];
  /** When the sidebar first listed the threads, the first usable frame's anchor. */
  readonly readyNanoseconds: number;
  readonly spawnedNanoseconds: number;
  readonly ticksPerSecond: number;
  readonly trace: string;
  /** The workload's own description, as the run measured it. */
  readonly workload: string;
}

interface PhaseResult {
  readonly compositorCpuTicks: number;
  readonly durationNanoseconds: number;
  readonly name: string;
  readonly pacing: FramePacing;
  readonly usage: TreeUsage;
}

interface RunResult {
  readonly discarded: number;
  readonly dmabufBuffers: number;
  /** Spawn to the first frame the compositor presented, whatever it showed. */
  readonly firstFrameNanoseconds: number | null;
  /** Spawn to the first frame presented after the sidebar first listed the threads. */
  readonly firstUsableFrameNanoseconds: number | null;
  readonly gpuFeatures: string;
  readonly phases: readonly PhaseResult[];
  readonly refreshHz: number;
  readonly shmBuffers: number;
  readonly ticksPerSecond: number;
  readonly workload: string;
}

/** All CPU ticks of a tree whatever the roles: the compositor's, which has no backend and no GPU process. */
const sumCpuTicks = (usage: TreeUsage): number => usage.backend.cpuTicks + usage.client.cpuTicks + usage.gpu.cpuTicks;

/** Chromium's GL renderer and its compositing and raster status, from `SystemInfo.getInfo`. */
const formatGpuFeatures = (gpuInfo: unknown): string => {
  const gpu = isRecord(gpuInfo) && isRecord(gpuInfo["gpu"]) ? gpuInfo["gpu"] : {};
  const status = isRecord(gpu["featureStatus"]) ? gpu["featureStatus"] : {};
  const renderer = isRecord(gpu["auxAttributes"]) ? gpu["auxAttributes"]["glRenderer"] : null;

  return [
    `GL renderer ${typeof renderer === "string" && renderer !== "" ? renderer : "unknown"}`,
    ...GPU_FEATURES.map((feature) => `${feature} ${String(status[feature] ?? "unknown")}`),
  ].join(", ");
};

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
    dmabufBuffers: log.dmabufBuffers,
    firstFrameNanoseconds: sinceSpawn(findFirstPresentedAfter(log, run.spawnedNanoseconds)),
    firstUsableFrameNanoseconds: sinceSpawn(findFirstPresentedAfter(log, run.readyNanoseconds)),
    gpuFeatures: formatGpuFeatures(run.gpuInfo),
    phases: run.phases.map((phase) => ({
      compositorCpuTicks: sumCpuTicks(phase.compositorUsage),
      durationNanoseconds: phase.endNanoseconds - phase.startNanoseconds,
      name: phase.name,
      pacing: summarizeFramePacing(log, phase, NANOSECONDS_PER_SECOND / refreshHz),
      usage: phase.usage,
    })),
    refreshHz,
    shmBuffers: log.shmBuffers,
    ticksPerSecond: run.ticksPerSecond,
    workload: run.workload,
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
    cpuMilliseconds(phase.compositorCpuTicks),
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
    `Chromium: ${result.gpuFeatures}. Buffers: ${String(result.shmBuffers)} wl_shm, ` +
      `${String(result.dmabufBuffers)} linux-dmabuf.`,
    result.workload,
    "",
    "| Phase | Duration (ms) | Frames | p50 (ms) | p95 (ms) | Max (ms) | Late | Client CPU (ms) | " +
      "GPU process CPU (ms) | Backend CPU (ms) | Compositor CPU (ms) | Client RSS (MiB) | Client PSS (MiB) | " +
      "Backend RSS (MiB) |",
    "| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |",
    ...result.phases.map((phase) => formatPhaseRow(phase, result.ticksPerSecond)),
    "",
  ].join("\n");

export { formatReport, summarizeRun };
export type { AppRun, MeasuredPhase };
