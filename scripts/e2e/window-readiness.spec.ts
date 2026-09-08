import {
  FIRST_PRESENTED_FRAME_TRACE_LINE,
  hasPresentedFirstFrame,
  waitForWindowReadyFailures,
} from "./window-readiness.ts";
import { describe, expect, it, vi } from "vitest";

const EMPTY_LENGTH = 0;
const TWO_CALLS = 2;

/** Echoes `isReady`'s own answer, so the caller controls which of the two signals it represents. */
const echoIsReady = (isReady: () => boolean): Promise<boolean> => Promise.resolve(isReady());

describe("hasPresentedFirstFrame", () => {
  it("reports false when the trace has not printed the line yet", () => {
    expect(hasPresentedFirstFrame("shadow-input: committed surface 1\n")).toBe(false);
  });

  it("reports true once the trace includes the line", () => {
    expect(hasPresentedFirstFrame(`shadow-input: committed surface 1\n${FIRST_PRESENTED_FRAME_TRACE_LINE}\n`)).toBe(
      true,
    );
  });
});

describe("waitForWindowReadyFailures when both signals arrive", () => {
  it("resolves to an empty array", async () => {
    const failures = await waitForWindowReadyFailures(
      { bundleReadyTraceLine: "app-ready", expectedExitTraceLine: null },
      { text: "" },
      () => Promise.resolve(true),
    );

    expect(failures).toHaveLength(EMPTY_LENGTH);
  });

  it("waits for both signals concurrently rather than one after the other", async () => {
    const wait = vi.fn(() => Promise.resolve(true));

    await waitForWindowReadyFailures(
      { bundleReadyTraceLine: "app-ready", expectedExitTraceLine: null },
      { text: "" },
      wait,
    );

    expect(wait).toHaveBeenCalledTimes(TWO_CALLS);
  });
});

describe("waitForWindowReadyFailures when one signal never arrives", () => {
  it("names only the window's line when the bundle's ready line arrives first", async () => {
    const failures = await waitForWindowReadyFailures(
      { bundleReadyTraceLine: "app-ready", expectedExitTraceLine: null },
      { text: "app-ready\n" },
      echoIsReady,
    );

    expect(failures).toEqual([`the window never printed "${FIRST_PRESENTED_FRAME_TRACE_LINE}"`]);
  });

  it("names only the bundle's line when the presented-frame line arrives first", async () => {
    const failures = await waitForWindowReadyFailures(
      { bundleReadyTraceLine: "app-ready", expectedExitTraceLine: null },
      { text: FIRST_PRESENTED_FRAME_TRACE_LINE },
      echoIsReady,
    );

    expect(failures).toEqual(['the bundle never printed "app-ready"']);
  });
});

describe("waitForWindowReadyFailures when neither signal arrives", () => {
  it("names both", async () => {
    const failures = await waitForWindowReadyFailures(
      { bundleReadyTraceLine: "app-ready", expectedExitTraceLine: null },
      { text: "" },
      () => Promise.resolve(false),
    );

    expect(failures).toEqual([
      'the bundle never printed "app-ready"',
      `the window never printed "${FIRST_PRESENTED_FRAME_TRACE_LINE}"`,
    ]);
  });
});

describe("waitForWindowReadyFailures for a scenario that expects the window to exit", () => {
  it("lets the expected-exit line stand in for the presented frame", async () => {
    const failures = await waitForWindowReadyFailures(
      { bundleReadyTraceLine: "app-ready", expectedExitTraceLine: "wayland protocol error" },
      { text: "app-ready\n[rnl-window] wayland protocol error: xdg_wm_base#7 code 4\n" },
      echoIsReady,
    );
    expect(failures).toHaveLength(EMPTY_LENGTH);
  });

  it("still names the missing presented frame when neither line arrives", async () => {
    const failures = await waitForWindowReadyFailures(
      { bundleReadyTraceLine: "app-ready", expectedExitTraceLine: "wayland protocol error" },
      { text: "app-ready\n" },
      echoIsReady,
    );
    expect(failures).toEqual([`the window never printed "${FIRST_PRESENTED_FRAME_TRACE_LINE}"`]);
  });
});
