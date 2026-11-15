import { describe, expect, it } from "vitest";
import { formatReport, summarizeRun } from "./report.ts";

const REFRESH_HZ = 120;
const LATER_THAN_EVERY_FRAME_NS = 11_000_000_000;
const usage = {
  backend: { cpuTicks: 112, pssKilobytes: 300_000, rssKilobytes: 460_800 },
  client: { cpuTicks: 252, pssKilobytes: 500_000, rssKilobytes: 819_200 },
  gpu: { cpuTicks: 131, pssKilobytes: 117_760, rssKilobytes: 153_600 },
};

const trace = [
  "[1.000] wp_presentation#14.clock_id(1)",
  "[2.000] wp_presentation_feedback#21.presented(0, 10, 0, 0, 0, 0, 0)",
  "[2.010] wp_presentation_feedback#22.presented(0, 10, 8333333, 0, 0, 0, 0)",
  "[2.020] wp_presentation_feedback#23.presented(0, 10, 25000000, 0, 0, 0, 0)",
  "",
].join("\n");

const run = {
  phases: [
    {
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
};

describe("summarizeRun", () => {
  it("measures startup from spawn and each phase's pacing against the output's refresh period", () => {
    expect(summarizeRun(run, REFRESH_HZ)).toEqual({
      discarded: 0,
      firstFrameNanoseconds: 1_000_000_000,
      firstUsableFrameNanoseconds: 1_008_333_333,
      phases: [
        {
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
      ticksPerSecond: 100,
    });
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
        "",
        "| Phase | Duration (ms) | Frames | p50 (ms) | p95 (ms) | Max (ms) | Late | Client CPU (ms) | " +
          "GPU process CPU (ms) | Backend CPU (ms) | Client RSS (MiB) | Client PSS (MiB) | Backend RSS (MiB) |",
        "| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |",
        "| Streamed reply | 100 | 3 | 8.33 | 16.67 | 16.67 | 1 | 2520 | 1310 | 1120 | 950 | 603 | 450 |",
        "",
      ].join("\n"),
    );
  });

  it("says never for a startup frame that did not happen", () => {
    expect(
      formatReport({
        discarded: 0,
        firstFrameNanoseconds: null,
        firstUsableFrameNanoseconds: null,
        phases: [],
        refreshHz: 120,
        ticksPerSecond: 100,
      }),
    ).toContain("First frame never, first usable frame never");
  });
});
