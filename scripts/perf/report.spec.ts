import { describe, expect, it } from "vitest";
import { describeRungMismatch, formatReport, summarizeRun } from "./report.ts";

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
  observedReadiness: { nanoseconds: 10_005_000_000, wasAlreadyPresent: false },
  phases: [
    {
      compositorUsage,
      endNanoseconds: 10_100_000_000,
      name: "Streamed reply",
      startNanoseconds: 10_000_000_000,
      usage,
    },
  ],
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
      gpuFeatures:
        "GL renderer llvmpipe (LLVM 20.1.2, 256 bits), gpu_compositing disabled_software, " +
        "rasterization disabled_software",
      observedReadyFrameNanoseconds: 1_008_333_333,
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
      readinessWasAlreadyPresent: false,
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

  it("has no frame after observed readiness when nothing was presented after it", () => {
    const observedReadiness = { nanoseconds: LATER_THAN_EVERY_FRAME_NS, wasAlreadyPresent: false };

    expect(summarizeRun({ ...run, observedReadiness }, REFRESH_HZ)?.observedReadyFrameNanoseconds).toBeNull();
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
        "Output 120 Hz. First frame 1000 ms, first frame after observed readiness 1008 ms, 0 frames discarded.",
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
        gpuFeatures: "",
        observedReadyFrameNanoseconds: null,
        phases: [],
        readinessWasAlreadyPresent: false,
        refreshHz: 120,
        shmBuffers: 0,
        ticksPerSecond: 100,
        workload: WORKLOAD,
      }),
    ).toContain("First frame never, first frame after observed readiness never");
  });
});

describe("formatReport, when the thread list was already there", () => {
  it("says readiness is the check's time, after React's commit", () => {
    const result = summarizeRun(
      { ...run, observedReadiness: { ...run.observedReadiness, wasAlreadyPresent: true } },
      REFRESH_HZ,
    );

    expect(result === null ? "" : formatReport(result)).toContain(
      "first frame after observed readiness 1008 ms (the thread list was already there when the check began, " +
        "so readiness is the check's time, after React's commit), 0 frames discarded.",
    );
  });
});

const RADEON = "ANGLE (AMD, AMD Radeon 890M (radeonsi gfx1150), OpenGL ES 3.2 Mesa 25.2.8)";
const LLVMPIPE = "ANGLE (Mesa, llvmpipe (LLVM 20.1.2 256 bits), OpenGL ES 3.2 Mesa 25.2.8)";

/** The `SystemInfo.getInfo` shape, with the three statuses the rungs were seen to report in the container. */
const reported = (renderer: string, compositing: string, rasterization: string): unknown => ({
  gpu: { auxAttributes: { glRenderer: renderer }, featureStatus: { gpu_compositing: compositing, rasterization } },
});
const SOFTWARE_PATH = reported("Disabled", "disabled_software", "disabled_software");
const LLVMPIPE_PATH = reported(LLVMPIPE, "disabled_software", "enabled");
const RADEON_RASTER_PATH = reported(RADEON, "disabled_software", "enabled");
const RADEON_PATH = reported(RADEON, "enabled", "enabled");

describe("describeRungMismatch", () => {
  it.each([
    ["software", SOFTWARE_PATH],
    ["llvmpipe", LLVMPIPE_PATH],
    ["gpu", RADEON_PATH],
  ] as const)("accepts the %s rung drawing the way it asks for", (rung, reportedInfo) => {
    expect(describeRungMismatch(rung, reportedInfo)).toBeNull();
  });

  it("rejects an llvmpipe run that ANGLE put on the Radeon", () => {
    expect(describeRungMismatch("llvmpipe", RADEON_RASTER_PATH)).toBe(
      `--renderer llvmpipe needs GPU raster on the llvmpipe GL renderer, but Chromium reported GL renderer ${RADEON}, ` +
        "gpu_compositing disabled_software, rasterization enabled, so the run does not count for that rung",
    );
  });

  it.each([
    ["llvmpipe", "fell back to software", SOFTWARE_PATH],
    ["gpu", "fell back to software", SOFTWARE_PATH],
    ["gpu", "composited on llvmpipe", reported(LLVMPIPE, "enabled", "enabled")],
    ["software", "rasterised on the Radeon", RADEON_RASTER_PATH],
    ["software", "composited on the Radeon", RADEON_PATH],
    ["software", "reported no GPU information", null],
  ] as const)("rejects a %s run that %s", (rung, _reason, reportedInfo) => {
    expect(describeRungMismatch(rung, reportedInfo)).toMatch(
      new RegExp(`^--renderer ${rung} needs .*, so the run does not count for that rung$`, "u"),
    );
  });
});
