import { describe, expect, it } from "vitest";
import { findFirstPresentedAfter, parsePresentationLog, summarizeFramePacing } from "./presentation.ts";

const REFRESH_120_HZ_NS = 8_333_333;
const FIRST_PRESENTED_NS = 191_010_672_442_967;
const SECOND_PRESENTED_NS = 191_010_683_000_000;
const HIGH_WORD_PRESENTED_NS = 4_294_967_296_000_000_000;
const BINDING = "[3349369.749] wp_presentation#14.clock_id(1)";

/** `presented(tv_sec_hi, tv_sec_lo, tv_nsec, refresh, seq_hi, seq_lo, flags)`; wlroots' headless refresh is 0. */
const presentedLine = (time: string): string =>
  `[3350138.721] wp_presentation_feedback#21.presented(${time}, 0, 0, 0, 0)`;

const parse = (...lines: readonly string[]): ReturnType<typeof parsePresentationLog> =>
  parsePresentationLog([BINDING, ...lines, ""].join("\n"));

/** Frames at 1.0 s and on: three at the refresh rate, one a refresh late, one 58 ms late; one each side of it. */
const scrollLog = parse(
  presentedLine("0, 0, 900000000"),
  presentedLine("0, 1, 0"),
  presentedLine("0, 1, 8333333"),
  presentedLine("0, 1, 16666666"),
  presentedLine("0, 1, 33333332"),
  presentedLine("0, 1, 41666665"),
  presentedLine("0, 1, 100000000"),
  presentedLine("0, 1, 200000000"),
);
const phase = { endNanoseconds: 1_100_000_000, name: "Feed scroll", startNanoseconds: 1_000_000_000 };
const noFrames = { discarded: 0, presentedNanoseconds: [] };

describe("parsePresentationLog", () => {
  it("reads presented timestamps and counts discarded frames, ignoring everything else in the trace", () => {
    const log = parse(
      "[3350138.700]  -> wl_surface#3.commit()",
      presentedLine("0, 191010, 672442967"),
      "[3350146.100] wp_presentation_feedback#29.discarded()",
      presentedLine("0, 191010, 683000000"),
      "DevTools listening on ws://127.0.0.1:9333/devtools/browser/x",
    );

    expect(log).toEqual({ discarded: 1, presentedNanoseconds: [FIRST_PRESENTED_NS, SECOND_PRESENTED_NS] });
  });

  it("carries tv_sec_hi as units of 2^32 seconds", () => {
    const log = parse(presentedLine("1, 0, 0"));

    expect(log?.presentedNanoseconds).toEqual([HIGH_WORD_PRESENTED_NS]);
  });

  it("returns null when the client never bound wp_presentation, which measured nothing", () => {
    expect(parsePresentationLog(presentedLine("0, 1, 0"))).toBeNull();
  });

  it("returns null on a clock other than CLOCK_MONOTONIC, which cannot be placed against the phases", () => {
    const realtimeTrace = ["[1.000] wp_presentation#14.clock_id(0)", presentedLine("0, 1, 0")].join("\n");

    expect(parsePresentationLog(realtimeTrace)).toBeNull();
  });
});

describe("summarizeFramePacing", () => {
  it("takes nearest-rank percentiles over the intervals between frames presented inside the phase", () => {
    expect(scrollLog === null ? null : summarizeFramePacing(scrollLog, phase, REFRESH_120_HZ_NS)).toEqual({
      frames: 6,
      lateFrames: 2,
      maximumNanoseconds: 58_333_335,
      medianNanoseconds: 8_333_333,
      percentile95Nanoseconds: 58_333_335,
    });
  });

  it("counts an interval as late only past one and a half refresh periods", () => {
    const log = parse(presentedLine("0, 1, 0"), presentedLine("0, 1, 12499999"), presentedLine("0, 1, 24999999"));

    expect(log === null ? null : summarizeFramePacing(log, phase, REFRESH_120_HZ_NS)).toMatchObject({ lateFrames: 1 });
  });

  it("reports zeroes for a phase with no frames rather than inventing intervals", () => {
    expect(summarizeFramePacing(noFrames, phase, REFRESH_120_HZ_NS)).toEqual({
      frames: 0,
      lateFrames: 0,
      maximumNanoseconds: 0,
      medianNanoseconds: 0,
      percentile95Nanoseconds: 0,
    });
  });
});

describe("findFirstPresentedAfter", () => {
  const log = { discarded: 0, presentedNanoseconds: [phase.startNanoseconds, phase.endNanoseconds] };

  it("finds the first frame presented at or after a moment", () => {
    expect(findFirstPresentedAfter(log, phase.startNanoseconds)).toBe(phase.startNanoseconds);
    expect(findFirstPresentedAfter(log, phase.startNanoseconds + REFRESH_120_HZ_NS)).toBe(phase.endNanoseconds);
  });

  it("returns null when nothing was presented after it", () => {
    expect(findFirstPresentedAfter(log, phase.endNanoseconds + REFRESH_120_HZ_NS)).toBeNull();
  });
});
