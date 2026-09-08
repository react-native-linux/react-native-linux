import { SURFACE_SCALE, logicalToSurfaceCoordinate, scalePointerSteps } from "./pointer-scale.ts";
import { describe, expect, it } from "vitest";

const SCALE_ONE = 1;
const SCALE_ONE_QUARTER = 1.25;
const SCALE_ONE_HALF = 1.5;
const SCALE_DOUBLE = 2;

/**
 * The same table #374's `WindowDecorationsTest.cpp` runs `roundedRatio` through, read as `logicalToSurfaceCoordinate`
 * inputs and outputs instead of `WindowExtent`s: identity at scale 1, exact grid points at 1.25 and 1.5, and the
 * off-grid case that only rounding half away from zero gets right.
 */
const conversionCases: readonly {
  logicalCoordinate: number;
  name: string;
  scale: number;
  surfaceCoordinate: number;
}[] = [
  { logicalCoordinate: 110, name: "identity at scale 1", scale: SCALE_ONE, surfaceCoordinate: 110 },
  { logicalCoordinate: 0, name: "zero at scale 1.5", scale: SCALE_ONE_HALF, surfaceCoordinate: 0 },
  { logicalCoordinate: 4, name: "on the scale 1.25 grid", scale: SCALE_ONE_QUARTER, surfaceCoordinate: 5 },
  { logicalCoordinate: 3, name: "off the scale 1.25 grid, rounds up", scale: SCALE_ONE_QUARTER, surfaceCoordinate: 4 },
  { logicalCoordinate: 2, name: "on the scale 1.5 grid", scale: SCALE_ONE_HALF, surfaceCoordinate: 3 },
  {
    logicalCoordinate: 5,
    name: "exactly half a pixel at scale 1.5, rounds away from zero",
    scale: SCALE_ONE_HALF,
    surfaceCoordinate: 8,
  },
  { logicalCoordinate: 3, name: "on the scale 2 grid", scale: SCALE_DOUBLE, surfaceCoordinate: 6 },
];

describe("logicalToSurfaceCoordinate", () => {
  for (const conversionCase of conversionCases) {
    it(conversionCase.name, () => {
      expect(logicalToSurfaceCoordinate(conversionCase.logicalCoordinate, conversionCase.scale)).toBe(
        conversionCase.surfaceCoordinate,
      );
    });
  }
});

describe("scalePointerSteps", () => {
  it("defaults to SURFACE_SCALE, which is 1 while #51 is unlanded", () => {
    const step = "move 110 140";

    expect(SURFACE_SCALE).toBe(SCALE_ONE);
    expect(scalePointerSteps([step])).toEqual([step]);
  });

  it("scales a move line's coordinates", () => {
    expect(scalePointerSteps(["move 4 5"], SCALE_ONE_QUARTER)).toEqual(["move 5 6"]);
  });

  it("scales a click line's coordinates", () => {
    expect(scalePointerSteps(["click 4 5"], SCALE_ONE_HALF)).toEqual(["click 6 8"]);
  });

  it("leaves non-pointer commands untouched", () => {
    const steps = ["sleep 500", "button left press", "wheel down 3", "key Tab press", "type hello", "# a comment"];

    expect(scalePointerSteps(steps, SCALE_DOUBLE)).toEqual(steps);
  });

  it("leaves a move line missing its y coordinate untouched", () => {
    expect(scalePointerSteps(["move 100"], SCALE_DOUBLE)).toEqual(["move 100"]);
  });

  it("leaves a bare click line with no coordinates untouched", () => {
    expect(scalePointerSteps(["click"], SCALE_DOUBLE)).toEqual(["click"]);
  });

  it("leaves a blank line untouched", () => {
    expect(scalePointerSteps([""], SCALE_DOUBLE)).toEqual([""]);
  });

  it("scales every move and click line in a full script", () => {
    expect(scalePointerSteps(["sleep 500", "move 110 140", "sleep 100", "click 110 140"], SCALE_DOUBLE)).toEqual([
      "sleep 500",
      "move 220 280",
      "sleep 100",
      "click 220 280",
    ]);
  });
});
