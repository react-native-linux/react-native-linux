import { afterEach, beforeEach, describe, expect, it } from "vitest";
import { mkdtempSync, rmSync, writeFileSync } from "node:fs";

import type { Scenario } from "./scenario.ts";
import { gradeArtifacts } from "./grade.ts";
import path from "node:path";
import { tmpdir } from "node:os";

/**
 * The frame journal's own grading (#345), split from `grade.spec.ts` rather than added to it: the two files
 * already sit at the repository's line-count ceiling, and journal grading is its own concern — a separate JSON
 * summary line, gated by its own scenario field — not an extension of `FrameTiming`'s. See *Frame journal* in
 * docs/cpp-toolchain.md.
 */

const MINIMUM_FRAMES = 60;
const BUDGET_P95_MS = 16.7;

const baseScenario: Scenario = {
  allowErrors: false,
  automation: null,
  bundle: "pressable.js",
  expect: ["pressable: topClick"],
  expectFailure: false,
  expectsWindowClose: false,
  frameBudget: null,
  frames: 600,
  name: "pressable-click",
  ready: "pressable: committed surface 1",
  screenshot: null,
  steps: ["sleep 500"],
  windowFlags: [],
};

const healthyFrameLogWithJournal = [
  '{"seq":1,"presentedNs":1000000000,"refreshNs":16666666,"flags":1}',
  '{"journal":true,"dirtyToPresentNs":11000000,"paintNs":3000000,"hang":false}',
  '{"summary":true,"frames":240,"discarded":0,"p50Ns":16000000,"p95Ns":16600000,"maxNs":31000000}',
  '{"journalSummary":true,"frames":238,"hangs":0,"p50Ns":11000000,"p95Ns":15000000,"maxNs":20000000}',
  "",
].join("\n");

const hangingFrameLogWithJournal = [
  '{"summary":true,"frames":240,"discarded":0,"p50Ns":16000000,"p95Ns":16600000,"maxNs":31000000}',
  '{"journalSummary":true,"frames":238,"hangs":3,"p50Ns":11000000,"p95Ns":15000000,"maxNs":20000000}',
  "",
].join("\n");

interface GradeInputs {
  readonly frameLogPath: string;
  readonly goldensDirectory: string;
  readonly scenario: Scenario;
  readonly screenshotPath: string;
}

let workspace = "";

const inputsFor = (scenario: Scenario): GradeInputs => ({
  frameLogPath: path.join(workspace, "frames.jsonl"),
  goldensDirectory: path.join(workspace, "goldens"),
  scenario,
  screenshotPath: path.join(workspace, "screenshot.png"),
});

const writeFrameLog = (text: string): void => {
  writeFileSync(path.join(workspace, "frames.jsonl"), text);
};

beforeEach(() => {
  workspace = mkdtempSync(path.join(tmpdir(), "rnl-grade-frame-journal-"));
});

afterEach(() => {
  rmSync(workspace, { force: true, recursive: true });
});

describe("gradeArtifacts frame journal", () => {
  it("notes the frame journal beside FrameTiming's own note when the log carries both", () => {
    writeFrameLog(healthyFrameLogWithJournal);

    expect(gradeArtifacts(inputsFor(baseScenario))).toEqual({
      failures: [],
      notes: [
        "240 frames, 0 discarded, p50 16.00 ms, p95 16.60 ms, max 31.00 ms",
        "238 journalled frames, 0 hangs, dirty-to-present p50 11.00 ms, p95 15.00 ms, max 20.00 ms",
      ],
    });
  });

  it("fails a run whose hangs exceed the scenario's maxHangs", () => {
    writeFrameLog(hangingFrameLogWithJournal);

    const scenario = {
      ...baseScenario,
      frameBudget: { maxHangs: 0, minFrames: MINIMUM_FRAMES, p95Ms: BUDGET_P95_MS },
    };

    expect(gradeArtifacts(inputsFor(scenario)).failures).toEqual([
      "3 frames hung past the frame journal's thresholds, the budget allows at most 0",
    ]);
  });
});
