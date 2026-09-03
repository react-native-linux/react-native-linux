import { afterEach, beforeEach, describe, expect, it } from "vitest";
import { mkdirSync, mkdtempSync, rmSync, writeFileSync } from "node:fs";

import { PNG } from "pngjs";

import type { Scenario } from "./scenario.ts";
import { gradeArtifacts } from "./grade.ts";
import path from "node:path";
import { tmpdir } from "node:os";

interface GradeInputs {
  readonly frameLogPath: string;
  readonly goldensDirectory: string;
  readonly scenario: Scenario;
  readonly screenshotPath: string;
}

const IMAGE_WIDTH = 4;
const IMAGE_HEIGHT = 4;
const OTHER_WIDTH = 8;
const BACKGROUND = 100;
const FAR_APART = 200;
const CHANNELS_PER_PIXEL = 4;
const NONE = 0;
const NO_BUDGET = 1;
const GENEROUS_BUDGET = 50;
const DIFFERENT_PIXELS = 2;
const MINIMUM_FRAMES = 60;
const BUDGET_P95_MS = 16.7;
const GOLDEN_NAME = "pressable-click.png";

const baseScenario: Scenario = {
  bundle: "pressable.js",
  expect: ["pressable: topClick"],
  frameBudget: null,
  frames: 600,
  name: "pressable-click",
  ready: "pressable: committed surface 1",
  screenshot: null,
  steps: ["sleep 500"],
};

const healthyFrameLog = [
  '{"seq":1,"presentedNs":1000000000,"refreshNs":16666666,"flags":1}',
  '{"summary":true,"frames":240,"discarded":0,"p50Ns":16000000,"p95Ns":16600000,"maxNs":31000000}',
  "",
].join("\n");

let workspace = "";

const encodePng = (width: number, height: number, differentPixels: number): Buffer => {
  const image = new PNG({ height, width });
  image.data.fill(BACKGROUND);
  image.data.fill(FAR_APART, NONE, differentPixels * CHANNELS_PER_PIXEL);

  return PNG.sync.write(image);
};

const inputsFor = (scenario: Scenario): GradeInputs => ({
  frameLogPath: path.join(workspace, "frames.jsonl"),
  goldensDirectory: path.join(workspace, "goldens"),
  scenario,
  screenshotPath: path.join(workspace, "screenshot.png"),
});

const writeFrameLog = (text: string): void => {
  writeFileSync(path.join(workspace, "frames.jsonl"), text);
};

const writeGolden = (differentPixels: number, width: number): void => {
  mkdirSync(path.join(workspace, "goldens"), { recursive: true });
  writeFileSync(path.join(workspace, "goldens", GOLDEN_NAME), encodePng(width, IMAGE_HEIGHT, differentPixels));
};

const writeScreenshot = (differentPixels: number): void => {
  writeFileSync(path.join(workspace, "screenshot.png"), encodePng(IMAGE_WIDTH, IMAGE_HEIGHT, differentPixels));
};

beforeEach(() => {
  workspace = mkdtempSync(path.join(tmpdir(), "rnl-grade-"));
});

afterEach(() => {
  rmSync(workspace, { force: true, recursive: true });
});

describe("gradeArtifacts frame timing", () => {
  it("notes the timings of a run with no declared budget", () => {
    writeFrameLog(healthyFrameLog);

    expect(gradeArtifacts(inputsFor(baseScenario))).toEqual({
      failures: [],
      notes: ["240 frames, 0 discarded, p50 16.00 ms, p95 16.60 ms, max 31.00 ms"],
    });
  });

  it("notes nothing when the window wrote no frame log at all", () => {
    expect(gradeArtifacts(inputsFor(baseScenario))).toEqual({ failures: [], notes: [] });
  });

  it("passes a run that met its budget", () => {
    writeFrameLog(healthyFrameLog);

    const scenario = { ...baseScenario, frameBudget: { minFrames: MINIMUM_FRAMES, p95Ms: BUDGET_P95_MS } };

    expect(gradeArtifacts(inputsFor(scenario)).failures).toEqual([]);
  });

  it("fails a run whose frame log never appeared", () => {
    const scenario = { ...baseScenario, frameBudget: { minFrames: MINIMUM_FRAMES, p95Ms: BUDGET_P95_MS } };

    expect(gradeArtifacts(inputsFor(scenario)).failures).toEqual([
      `the window wrote no frame-timing summary to ${path.join(workspace, "frames.jsonl")}`,
    ]);
  });
});

describe("gradeArtifacts screenshots", () => {
  const scenario: Scenario = {
    ...baseScenario,
    screenshot: { golden: GOLDEN_NAME, maxDifferentPixels: GENEROUS_BUDGET },
  };

  it("grades nothing when the scenario declares no comparison", () => {
    expect(gradeArtifacts(inputsFor(baseScenario)).notes).toEqual([]);
  });

  it("names the picture to bless when the golden does not exist yet", () => {
    writeScreenshot(NONE);

    expect(gradeArtifacts(inputsFor(scenario)).notes).toEqual([
      `no screenshot golden at ${path.join(workspace, "goldens", GOLDEN_NAME)}; ` +
        `bless ${path.join(workspace, "screenshot.png")}`,
    ]);
  });

  it("fails when the golden exists and the window captured nothing", () => {
    writeGolden(NONE, IMAGE_WIDTH);

    expect(gradeArtifacts(inputsFor(scenario)).failures).toEqual([
      `the window captured no screenshot at ${path.join(workspace, "screenshot.png")}`,
    ]);
  });
});

describe("gradeArtifacts screenshot comparison", () => {
  const scenario: Scenario = {
    ...baseScenario,
    screenshot: { golden: GOLDEN_NAME, maxDifferentPixels: GENEROUS_BUDGET },
  };

  it("passes a screenshot that matches its golden", () => {
    writeGolden(NONE, IMAGE_WIDTH);
    writeScreenshot(NONE);

    expect(gradeArtifacts(inputsFor(scenario))).toEqual({ failures: [], notes: [] });
  });

  it("fails a screenshot that differs by more than the scenario allows", () => {
    writeGolden(NONE, IMAGE_WIDTH);
    writeScreenshot(DIFFERENT_PIXELS);

    const strict = { ...scenario, screenshot: { golden: GOLDEN_NAME, maxDifferentPixels: NO_BUDGET } };

    expect(gradeArtifacts(inputsFor(strict)).failures).toEqual([
      `the screenshot does not match ${GOLDEN_NAME}: 2 pixels differ by more than 8 per channel, the budget is 1`,
    ]);
  });

  it("fails a screenshot the compositor sized differently from the golden", () => {
    writeGolden(NONE, OTHER_WIDTH);
    writeScreenshot(NONE);

    expect(gradeArtifacts(inputsFor(scenario)).failures).toEqual([
      `the screenshot does not match ${GOLDEN_NAME}: the screenshot is 4x4 and the golden is 8x4`,
    ]);
  });
});
