import { clampNumber, rangeFraction, snapToStep, valueAtOffset } from "./src/components/ui/controlValues.linux.ts";
import { describe, expect, it } from "vitest";

const FONT_MIN = 12;
const FONT_MAX = 24;
const FONT_STEP = 1;
const FONT_THIRD = 16;
const BELOW_FONT_MIN = 11;
const ABOVE_FONT_MAX = 30;
const JUST_UNDER_HALF_STEP_ABOVE_THIRD = 16.4;
const HALF_STEP_ABOVE_THIRD = 16.5;
const QUARTER = 0.25;
const JUST_OVER_QUARTER = 0.26;
const UNEVEN_MAX = 10;
const UNEVEN_STEP = 4;
const PAST_UNEVEN_MAX = 11.5;
const FONT_QUARTER = 15;
const TRACK_WIDTH = 300;
const TRACK_THIRD = 100;
const BEFORE_TRACK = -20;
const PAST_TRACK = 400;
const NO_WIDTH = 0;
const WHOLE = 1;
const NONE = 0;

describe("clampNumber", () => {
  it("keeps a value inside the range and moves one outside it to the nearer end", () => {
    expect([
      clampNumber(FONT_THIRD, FONT_MIN, FONT_MAX),
      clampNumber(BELOW_FONT_MIN, FONT_MIN, FONT_MAX),
      clampNumber(ABOVE_FONT_MAX, FONT_MIN, FONT_MAX),
    ]).toStrictEqual([FONT_THIRD, FONT_MIN, FONT_MAX]);
  });
});

describe("snapToStep", () => {
  it("rounds to the nearest step counted from the minimum", () => {
    expect([
      snapToStep(JUST_UNDER_HALF_STEP_ABOVE_THIRD, FONT_MIN, FONT_MAX, FONT_STEP),
      snapToStep(HALF_STEP_ABOVE_THIRD, FONT_MIN, FONT_MAX, FONT_STEP),
      snapToStep(JUST_OVER_QUARTER, NONE, WHOLE, QUARTER),
    ]).toStrictEqual([FONT_THIRD, FONT_THIRD + FONT_STEP, QUARTER]);
  });

  it("never leaves the range, even for a step that does not divide it", () => {
    expect([
      snapToStep(ABOVE_FONT_MAX, FONT_MIN, FONT_MAX, FONT_STEP),
      snapToStep(BELOW_FONT_MIN, FONT_MIN, FONT_MAX, FONT_STEP),
      snapToStep(PAST_UNEVEN_MAX, NONE, UNEVEN_MAX, UNEVEN_STEP),
    ]).toStrictEqual([FONT_MAX, FONT_MIN, UNEVEN_MAX]);
  });
});

describe("rangeFraction", () => {
  it("places a value along the range and clamps one outside it", () => {
    expect([
      rangeFraction(FONT_QUARTER, FONT_MIN, FONT_MAX),
      rangeFraction(ABOVE_FONT_MAX, FONT_MIN, FONT_MAX),
      rangeFraction(BELOW_FONT_MIN, FONT_MIN, FONT_MAX),
    ]).toStrictEqual([QUARTER, WHOLE, NONE]);
  });

  it("reads an empty range as its start", () => {
    expect(rangeFraction(FONT_MIN, FONT_MIN, FONT_MIN)).toBe(NONE);
  });
});

describe("valueAtOffset", () => {
  it("maps a pointer offset along the track onto the range, clamped to its ends", () => {
    expect([
      valueAtOffset(TRACK_THIRD, TRACK_WIDTH, FONT_MIN, FONT_MAX),
      valueAtOffset(BEFORE_TRACK, TRACK_WIDTH, FONT_MIN, FONT_MAX),
      valueAtOffset(PAST_TRACK, TRACK_WIDTH, FONT_MIN, FONT_MAX),
    ]).toStrictEqual([FONT_THIRD, FONT_MIN, FONT_MAX]);
  });

  it("answers the minimum before the track has a width", () => {
    expect(valueAtOffset(TRACK_THIRD, NO_WIDTH, FONT_MIN, FONT_MAX)).toBe(FONT_MIN);
  });
});
