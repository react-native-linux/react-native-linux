import { describe, expect, it } from "vitest";
import { parseScenario } from "./scenario.ts";

const MAX_DIFFERENT_PIXELS = 50;

const validScenario = {
  bundle: "pressable.js",
  expect: ["pressable: topClick on box at 200,140"],
  name: "pressable-click",
  ready: "pressable: committed surface 1",
  steps: ["sleep 500", "click 200 140"],
};

const screenshot = { golden: "pressable-click.png", maxDifferentPixels: MAX_DIFFERENT_PIXELS };
const crop = { height: 140, left: 90, top: 70, width: 220 };

describe("parseScenario screenshot crop", () => {
  it("reads a crop rectangle, including a zero coordinate", () => {
    const zeroed = { ...crop, left: 0, top: 0 };

    expect(
      parseScenario({ ...validScenario, screenshot: { ...screenshot, crop: zeroed } }, "fixture.json").screenshot,
    ).toEqual({ ...screenshot, crop: zeroed });
  });

  it("defaults the crop to null when the scenario declares none", () => {
    expect(parseScenario({ ...validScenario, screenshot }, "fixture.json").screenshot).toEqual({
      ...screenshot,
      crop: null,
    });
  });

  it("rejects a crop left that is not a number", () => {
    expect(() =>
      parseScenario({ ...validScenario, screenshot: { ...screenshot, crop: { ...crop, left: "90" } } }, "f.json"),
    ).toThrow('f.json: "screenshot.crop.left" must be a non-negative integer');
  });

  it("rejects a fractional crop top", () => {
    expect(() =>
      parseScenario({ ...validScenario, screenshot: { ...screenshot, crop: { ...crop, top: 1.5 } } }, "f.json"),
    ).toThrow('f.json: "screenshot.crop.top" must be a non-negative integer');
  });

  it("rejects a negative crop left", () => {
    expect(() =>
      parseScenario({ ...validScenario, screenshot: { ...screenshot, crop: { ...crop, left: -1 } } }, "f.json"),
    ).toThrow('f.json: "screenshot.crop.left" must be a non-negative integer');
  });
});
