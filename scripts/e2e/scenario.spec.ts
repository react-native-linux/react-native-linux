import { describe, expect, it } from "vitest";
import { findMissingExpectations, formatInjectorScript, parseScenario, resolveArtifactPaths } from "./scenario.ts";

const DEFAULT_FRAME_COUNT = 600;
const EXPLICIT_FRAME_COUNT = 120;
const FRACTIONAL_FRAME_COUNT = 1.5;
const NO_FRAMES = 0;
const MINIMUM_FRAMES = 60;
const BUDGET_P95_MS = 16.7;
const MAX_DIFFERENT_PIXELS = 50;

const validScenario = {
  bundle: "pressable.js",
  expect: ["pressable: topClick on box at 200,140"],
  name: "pressable-click",
  ready: "pressable: committed surface 1",
  steps: ["sleep 500", "click 200 140"],
};

describe("parseScenario", () => {
  it("reads every field of a valid scenario", () => {
    expect(parseScenario({ ...validScenario, frames: EXPLICIT_FRAME_COUNT }, "fixture.json")).toEqual({
      bundle: "pressable.js",
      expect: ["pressable: topClick on box at 200,140"],
      frameBudget: null,
      frames: EXPLICIT_FRAME_COUNT,
      name: "pressable-click",
      ready: "pressable: committed surface 1",
      screenshot: null,
      steps: ["sleep 500", "click 200 140"],
    });
  });

  it("defaults the frame budget", () => {
    expect(parseScenario(validScenario, "fixture.json").frames).toBe(DEFAULT_FRAME_COUNT);
  });
});

describe("parseScenario shape rejections", () => {
  it("rejects a scenario that is not an object", () => {
    expect(() => parseScenario("pressable", "fixture.json")).toThrow("fixture.json: a scenario must be a JSON object");
  });

  it("rejects null", () => {
    expect(() => parseScenario(null, "fixture.json")).toThrow("a scenario must be a JSON object");
  });

  it("rejects an array", () => {
    expect(() => parseScenario([], "fixture.json")).toThrow("a scenario must be a JSON object");
  });
});

describe("parseScenario field rejections", () => {
  it("rejects a missing string field", () => {
    const withoutName = { bundle: "pressable.js", expect: ["click"], ready: "ready", steps: ["sleep 1"] };

    expect(() => parseScenario(withoutName, "fixture.json")).toThrow('fixture.json: "name" must be a non-empty string');
  });

  it("rejects an empty string field", () => {
    expect(() => parseScenario({ ...validScenario, bundle: "" }, "fixture.json")).toThrow(
      'fixture.json: "bundle" must be a non-empty string',
    );
  });

  it("rejects a steps field that is not an array", () => {
    expect(() => parseScenario({ ...validScenario, steps: "click 1 1" }, "fixture.json")).toThrow(
      'fixture.json: "steps" must be a non-empty array of strings',
    );
  });

  it("rejects an array that holds something other than strings", () => {
    expect(() => parseScenario({ ...validScenario, expect: [true] }, "fixture.json")).toThrow(
      'fixture.json: "expect" must be a non-empty array of strings',
    );
  });

  it("rejects an empty array", () => {
    expect(() => parseScenario({ ...validScenario, expect: [] }, "fixture.json")).toThrow(
      'fixture.json: "expect" must be a non-empty array of strings',
    );
  });
});

describe("parseScenario frame budget rejections", () => {
  it("rejects a frame budget that is not a number", () => {
    expect(() => parseScenario({ ...validScenario, frames: "600" }, "fixture.json")).toThrow(
      'fixture.json: "frames" must be a positive integer',
    );
  });

  it("rejects a fractional frame budget", () => {
    expect(() => parseScenario({ ...validScenario, frames: FRACTIONAL_FRAME_COUNT }, "fixture.json")).toThrow(
      'fixture.json: "frames" must be a positive integer',
    );
  });

  it("rejects a frame budget below one", () => {
    expect(() => parseScenario({ ...validScenario, frames: NO_FRAMES }, "fixture.json")).toThrow(
      'fixture.json: "frames" must be a positive integer',
    );
  });
});

describe("parseScenario frameBudget", () => {
  const frameBudget = { minFrames: MINIMUM_FRAMES, p95Ms: BUDGET_P95_MS };

  it("reads a perf budget", () => {
    expect(parseScenario({ ...validScenario, frameBudget }, "fixture.json").frameBudget).toEqual(frameBudget);
  });

  it("rejects a perf budget that is not an object", () => {
    expect(() => parseScenario({ ...validScenario, frameBudget: BUDGET_P95_MS }, "fixture.json")).toThrow(
      'fixture.json: "frameBudget" must be a JSON object',
    );
  });

  it("rejects a fractional minimum frame count", () => {
    expect(() =>
      parseScenario({ ...validScenario, frameBudget: { ...frameBudget, minFrames: FRACTIONAL_FRAME_COUNT } }, "f.json"),
    ).toThrow('f.json: "frameBudget.minFrames" must be a positive integer');
  });

  it("rejects a p95 that is not a number", () => {
    expect(() =>
      parseScenario({ ...validScenario, frameBudget: { ...frameBudget, p95Ms: "16.7" } }, "fixture.json"),
    ).toThrow('fixture.json: "frameBudget.p95Ms" must be a positive number');
  });

  it("rejects a p95 of zero", () => {
    expect(() =>
      parseScenario({ ...validScenario, frameBudget: { ...frameBudget, p95Ms: NO_FRAMES } }, "fixture.json"),
    ).toThrow('fixture.json: "frameBudget.p95Ms" must be a positive number');
  });

  it("rejects a p95 that is not finite", () => {
    expect(() =>
      parseScenario({ ...validScenario, frameBudget: { ...frameBudget, p95Ms: Number.POSITIVE_INFINITY } }, "f.json"),
    ).toThrow('f.json: "frameBudget.p95Ms" must be a positive number');
  });
});

describe("parseScenario screenshot", () => {
  const screenshot = { golden: "pressable-click.png", maxDifferentPixels: MAX_DIFFERENT_PIXELS };

  it("reads a screenshot comparison", () => {
    expect(parseScenario({ ...validScenario, screenshot }, "fixture.json").screenshot).toEqual(screenshot);
  });

  it("rejects a screenshot comparison that is not an object", () => {
    expect(() => parseScenario({ ...validScenario, screenshot: "pressable-click.png" }, "fixture.json")).toThrow(
      'fixture.json: "screenshot" must be a JSON object',
    );
  });

  it("rejects a missing golden name", () => {
    expect(() =>
      parseScenario({ ...validScenario, screenshot: { maxDifferentPixels: MAX_DIFFERENT_PIXELS } }, "fixture.json"),
    ).toThrow('fixture.json: "screenshot.golden" must be a non-empty string');
  });

  it("rejects a pixel budget that is not a positive integer", () => {
    expect(() =>
      parseScenario({ ...validScenario, screenshot: { ...screenshot, maxDifferentPixels: NO_FRAMES } }, "fixture.json"),
    ).toThrow('fixture.json: "screenshot.maxDifferentPixels" must be a positive integer');
  });
});

describe("formatInjectorScript", () => {
  it("writes one step per line and terminates the last one", () => {
    expect(formatInjectorScript(["sleep 500", "click 200 140"])).toBe("sleep 500\nclick 200 140\n");
  });
});

describe("findMissingExpectations", () => {
  const trace = ["pressable: committed surface 1", "pressable: topPointerDown on box", "pressable: topClick on box"];

  it("reports nothing when every expectation appears in order", () => {
    expect(findMissingExpectations(trace, ["topPointerDown", "topClick"])).toEqual([]);
  });

  it("reports an expectation the trace never produced", () => {
    expect(findMissingExpectations(trace, ["topKeyPress"])).toEqual(["topKeyPress"]);
  });

  it("reports an expectation that only appears before the one it has to follow", () => {
    expect(findMissingExpectations(trace, ["topClick", "topPointerDown"])).toEqual(["topPointerDown"]);
  });
});

describe("resolveArtifactPaths", () => {
  it("names the artifacts after the scenario", () => {
    expect(resolveArtifactPaths("build/e2e", "pressable-click")).toEqual({
      directory: "build/e2e/pressable-click",
      frameLogPath: "build/e2e/pressable-click/frames.jsonl",
      screenshotPath: "build/e2e/pressable-click/screenshot.png",
      tracePath: "build/e2e/pressable-click/trace.log",
    });
  });
});
