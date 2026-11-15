import { buildWheelSteps, checkScrollTravel, formatScrollWorkload } from "./scroll.ts";
import { describe, expect, it } from "vitest";

const NOTCHES = 16;
const LINES_PER_NOTCH = 2;
const POINTER_LINES = 2;
const FIRST_STEP = 0;
const LAST_STEP = -1;
const BOTTOM = 3000;
const TRAVEL = 1800;
const UNDER_TOLERANCE = 0.5;
const OVER_TOLERANCE = 2;
const ROUNDED_AWAY = 0.4;
const FIRST_NOTCH = ["move 640 401", "wheel up 1", "sleep 60"];
const travelled = { bottom: BOTTOM, end: BOTTOM, start: BOTTOM, top: BOTTOM - TRAVEL };

describe("buildWheelSteps", () => {
  it("puts the pointer over the feed in whole output pixels, sends one notch per interval, then holds it", () => {
    const steps = buildWheelSteps({ xPixels: 640.4, yPixels: 400.6 }, "up");

    expect(steps).toHaveLength(POINTER_LINES + NOTCHES * LINES_PER_NOTCH);
    expect(steps.slice(FIRST_STEP, FIRST_NOTCH.length)).toEqual(FIRST_NOTCH);
    expect(new Set(steps.slice(FIRST_NOTCH.length - LINES_PER_NOTCH, LAST_STEP))).toEqual(
      new Set(["wheel up 1", "sleep 60"]),
    );
    expect(steps.at(LAST_STEP)).toBe("sleep 400");
  });
});

describe("checkScrollTravel", () => {
  it("gives the distance travelled each way when the feed starts and ends at its bottom", () => {
    expect(checkScrollTravel({ ...travelled, end: BOTTOM - UNDER_TOLERANCE })).toBe(TRAVEL);
  });

  it.each([
    ["started above its bottom", { ...travelled, start: BOTTOM - OVER_TOLERANCE }, "did not start and end"],
    ["did not come back down", { ...travelled, end: BOTTOM - TRAVEL }, "did not start and end"],
    ["hit the top, so later notches moved nothing", { ...travelled, top: 0 }, "within its range"],
    ["did not move at all", { ...travelled, top: BOTTOM }, "within its range"],
  ])("rejects a run whose feed %s", (_reason, positions, message) => {
    expect(checkScrollTravel(positions)).toContain(message);
  });
});

describe("formatScrollWorkload", () => {
  it("states the notch count, the rate and the measured distance", () => {
    expect(formatScrollWorkload(TRAVEL + ROUNDED_AWAY)).toBe(
      "Feed scroll: 16 wheel notches up, then 16 down, one every 60 ms, the pointer held 400 ms after the last, " +
        "1800 px each way.",
    );
  });
});
