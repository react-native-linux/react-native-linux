import { describe, expect, it } from "vitest";
import { findFrameBudgetFailures, parseFrameJournalSummary, parseFrameLogSummary } from "./frame-log.ts";
import { parseScenario } from "./scenario.ts";

/**
 * `frameBudget.maxJournalledFrames` (#42): the cap on frames that painted damage, which is how `resize-settle`
 * says each configure painted once and nothing painted after the drag. Split from `frame-log.spec.ts` and
 * `scenario-frame-budget.spec.ts`, which sit at the repository's line-count ceiling.
 */

const FRAME_LOG_PATH = "build/e2e/resize-settle/frames.jsonl";
const MAX_JOURNALLED_FRAMES = 11;
const OVER_BUDGET_FRAMES = 12;
const CONTINUOUS_REPAINT_FRAMES = 500;

const summary = parseFrameLogSummary(
  '{"summary":true,"frames":599,"discarded":0,"p50Ns":16000000,"p95Ns":16300000,"maxNs":28000000}',
);
const journalSummaryOf = (frames: number): ReturnType<typeof parseFrameJournalSummary> =>
  parseFrameJournalSummary(
    `{"journalSummary":true,"frames":${String(frames)},"hangs":0,"p50Ns":14000000,"p95Ns":23000000,"maxNs":23000000}`,
  );

const settleBudget = {
  maxHangs: null,
  maxJournalledFrames: MAX_JOURNALLED_FRAMES,
  minFrames: 60,
  p95Ms: 17.5,
};

describe("findFrameBudgetFailures journalled-frame budget", () => {
  it("does not gate on painted frames when the scenario sets no maxJournalledFrames", () => {
    expect(
      findFrameBudgetFailures({
        budget: { ...settleBudget, maxJournalledFrames: null },
        frameLogPath: FRAME_LOG_PATH,
        journalSummary: journalSummaryOf(CONTINUOUS_REPAINT_FRAMES),
        summary,
      }),
    ).toEqual([]);
  });

  it("reports a run with no journal summary when the scenario gates on painted frames", () => {
    expect(
      findFrameBudgetFailures({ budget: settleBudget, frameLogPath: FRAME_LOG_PATH, journalSummary: null, summary }),
    ).toEqual([`the window wrote no frame-journal summary to ${FRAME_LOG_PATH}`]);
  });

  it("reports nothing when every painted frame is within the budget", () => {
    expect(
      findFrameBudgetFailures({
        budget: settleBudget,
        frameLogPath: FRAME_LOG_PATH,
        journalSummary: journalSummaryOf(MAX_JOURNALLED_FRAMES),
        summary,
      }),
    ).toEqual([]);
  });

  it("reports painted frames over the budget", () => {
    expect(
      findFrameBudgetFailures({
        budget: settleBudget,
        frameLogPath: FRAME_LOG_PATH,
        journalSummary: journalSummaryOf(OVER_BUDGET_FRAMES),
        summary,
      }),
    ).toEqual(["12 frames painted damage, the budget allows at most 11"]);
  });
});

describe("parseScenario frameBudget maxJournalledFrames", () => {
  const scenario = {
    bundle: "resize-settle.js",
    expect: ["resize-settle: committed surface 1"],
    name: "resize-settle",
    ready: "resize-settle: committed surface 1",
    steps: ["sleep 4000"],
  };
  const frameBudget = { minFrames: 60, p95Ms: 17.5 };

  it("defaults to null when the scenario omits it", () => {
    expect(parseScenario({ ...scenario, frameBudget }, "fixture.json").frameBudget?.maxJournalledFrames).toBeNull();
  });

  it("reads an explicit cap", () => {
    expect(
      parseScenario(
        { ...scenario, frameBudget: { ...frameBudget, maxJournalledFrames: MAX_JOURNALLED_FRAMES } },
        "fixture.json",
      ).frameBudget?.maxJournalledFrames,
    ).toBe(MAX_JOURNALLED_FRAMES);
  });

  it("rejects a negative cap", () => {
    expect(() =>
      parseScenario({ ...scenario, frameBudget: { ...frameBudget, maxJournalledFrames: -1 } }, "fixture.json"),
    ).toThrow('fixture.json: "frameBudget.maxJournalledFrames" must be a non-negative integer');
  });
});
