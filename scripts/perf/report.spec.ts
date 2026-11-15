import { describe, expect, it } from "vitest";
import { formatReport, summarizeRun } from "./report.ts";

const REFRESH_HZ = 120;
const LATER_THAN_EVERY_FRAME_NS = 11_000_000_000;
const usage = {
  backend: { cpuTicks: 112, pssKilobytes: 300_000, rssKilobytes: 460_800 },
  client: { cpuTicks: 252, pssKilobytes: 500_000, rssKilobytes: 819_200 },
  gpu: { cpuTicks: 131, pssKilobytes: 117_760, rssKilobytes: 153_600 },
};

const compositorUsage = {
  backend: { cpuTicks: 0, pssKilobytes: 0, rssKilobytes: 0 },
  client: { cpuTicks: 40, pssKilobytes: 9000, rssKilobytes: 30_000 },
  gpu: { cpuTicks: 2, pssKilobytes: 0, rssKilobytes: 0 },
};
const gpuInfo = {
  gpu: {
    auxAttributes: { glRenderer: "llvmpipe (LLVM 20.1.2, 256 bits)" },
    featureStatus: { gpu_compositing: "disabled_software", rasterization: "disabled_software", webgl: "enabled" },
  },
};
const WORKLOAD = "Feed scroll: 16 wheel notches up, then 16 down, one every 60 ms, 1800 px each way.";

const trace = [
  "[1.000] wp_presentation#14.clock_id(1)",
  "[1.500]  -> wl_shm_pool#45.create_buffer(new id wl_buffer#50, 0, 1280, 720, 5120, 0)",
  "[2.000] wp_presentation_feedback#21.presented(0, 10, 0, 0, 0, 0, 0)",
  "[2.010] wp_presentation_feedback#22.presented(0, 10, 8333333, 0, 0, 0, 0)",
  "[2.020] wp_presentation_feedback#23.presented(0, 10, 25000000, 0, 0, 0, 0)",
  "",
].join("\n");

const run = {
  gpuInfo,
  phases: [
    {
      compositorUsage,
      endNanoseconds: 10_100_000_000,
      name: "Streamed reply",
      startNanoseconds: 10_000_000_000,
      usage,
    },
  ],
  readyNanoseconds: 10_005_000_000,
  spawnedNanoseconds: 9_000_000_000,
  ticksPerSecond: 100,
  trace,
  workload: WORKLOAD,
};

describe("summarizeRun", () => {
  it("measures startup from spawn and each phase's pacing against the output's refresh period", () => {
    expect(summarizeRun(run, REFRESH_HZ)).toEqual({
      discarded: 0,
      dmabufBuffers: 0,
      firstFrameNanoseconds: 1_000_000_000,
      firstUsableFrameNanoseconds: 1_008_333_333,
      gpuFeatures:
        "GL renderer llvmpipe (LLVM 20.1.2, 256 bits), gpu_compositing disabled_software, " +
        "rasterization disabled_software",
      phases: [
        {
          compositorCpuTicks: 42,
          durationNanoseconds: 100_000_000,
          name: "Streamed reply",
          pacing: {
            frames: 3,
            lateFrames: 1,
            maximumNanoseconds: 16_666_667,
            medianNanoseconds: 8_333_333,
            percentile95Nanoseconds: 16_666_667,
          },
          usage,
        },
      ],
      refreshHz: 120,
      shmBuffers: 1,
      ticksPerSecond: 100,
      workload: WORKLOAD,
    });
  });
});

describe("summarizeRun, when something was not reported", () => {
  it.each([
    ["no GPU information", null],
    ["no feature status or renderer", { gpu: { auxAttributes: { glRenderer: "" } } }],
    ["no auxiliary attributes", { gpu: { featureStatus: {} } }],
  ])("says unknown for what Chromium did not report when it gave %s", (_reason, partialGpuInfo) => {
    expect(summarizeRun({ ...run, gpuInfo: partialGpuInfo }, REFRESH_HZ)?.gpuFeatures).toBe(
      "GL renderer unknown, gpu_compositing unknown, rasterization unknown",
    );
  });

  it("has no first usable frame when nothing was presented after the app was ready", () => {
    expect(
      summarizeRun({ ...run, readyNanoseconds: LATER_THAN_EVERY_FRAME_NS }, REFRESH_HZ)?.firstUsableFrameNanoseconds,
    ).toBeNull();
  });

  it("returns null for a trace without wp_presentation, which measured nothing", () => {
    expect(summarizeRun({ ...run, trace: "DevTools listening\n" }, REFRESH_HZ)).toBeNull();
  });
});

describe("formatReport", () => {
  it("prints startup, then one Markdown row per phase with CPU in milliseconds and memory in MiB", () => {
    const result = summarizeRun(run, REFRESH_HZ);

    expect(result === null ? "" : formatReport(result)).toBe(
      [
        "Output 120 Hz. First frame 1000 ms, first usable frame 1008 ms, 0 frames discarded.",
        "Chromium: GL renderer llvmpipe (LLVM 20.1.2, 256 bits), gpu_compositing disabled_software, " +
          "rasterization disabled_software. Buffers: 1 wl_shm, 0 linux-dmabuf.",
        WORKLOAD,
        "",
        "| Phase | Duration (ms) | Frames | p50 (ms) | p95 (ms) | Max (ms) | Late | Client CPU (ms) | " +
          "GPU process CPU (ms) | Backend CPU (ms) | Compositor CPU (ms) | Client RSS (MiB) | Client PSS (MiB) | " +
          "Backend RSS (MiB) |",
        "| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |",
        "| Streamed reply | 100 | 3 | 8.33 | 16.67 | 16.67 | 1 | 2520 | 1310 | 1120 | 420 | 950 | 603 | 450 |",
        "",
      ].join("\n"),
    );
  });

  it("says never for a startup frame that did not happen", () => {
    expect(
      formatReport({
        discarded: 0,
        dmabufBuffers: 0,
        firstFrameNanoseconds: null,
        firstUsableFrameNanoseconds: null,
        gpuFeatures: "",
        phases: [],
        refreshHz: 120,
        shmBuffers: 0,
        ticksPerSecond: 100,
        workload: WORKLOAD,
      }),
    ).toContain("First frame never, first usable frame never");
  });
});
