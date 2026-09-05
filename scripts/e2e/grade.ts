import { cropImage, findScreenshotFailure } from "./screenshot.ts";
import { describeFrameTiming, findFrameBudgetFailures, parseFrameLogSummary } from "./frame-log.ts";
import { existsSync, readFileSync } from "node:fs";

import { PNG } from "pngjs";
import type { Scenario } from "./scenario.ts";
import path from "node:path";

const NO_TEXT = "";

/**
 * What a scenario's artifacts say once the run is over, split so a note never fails a run and a failure is never
 * only a note. The pure halves live in `frame-log.ts` and `screenshot.ts`; this is the file reading around them.
 */
interface Grade {
  readonly failures: readonly string[];
  readonly notes: readonly string[];
}

interface GradeInputs {
  readonly frameLogPath: string;
  readonly goldensDirectory: string;
  readonly scenario: Scenario;
  readonly screenshotPath: string;
}

const readPixelImage = (imagePath: string): { data: Uint8Array; height: number; width: number } => {
  const decoded = PNG.sync.read(readFileSync(imagePath));

  return { data: decoded.data, height: decoded.height, width: decoded.width };
};

/**
 * The perf gate of #7. The numbers are `wp_presentation` feedback the window wrote as it presented, not anything
 * the driver timed with a wall clock. See *Frame timing* in docs/cpp-toolchain.md.
 */
const gradeFrameTiming = (scenario: Scenario, frameLogPath: string): Grade => {
  const frameLogText = existsSync(frameLogPath) ? readFileSync(frameLogPath, "utf8") : NO_TEXT;
  const summary = parseFrameLogSummary(frameLogText);
  const notes = summary === null ? [] : [describeFrameTiming(summary)];

  if (scenario.frameBudget === null) {
    return { failures: [], notes };
  }

  return { failures: findFrameBudgetFailures(summary, scenario.frameBudget, frameLogPath), notes };
};

/**
 * Narrows the captured screenshot to the scenario's crop rectangle, if it names one, before comparing it against
 * the already-cropped golden. A crop that does not fit inside the capture is reported the same way a size or
 * pixel mismatch is: as a failure naming the golden, not as a thrown error.
 */
const compareScreenshot = (
  screenshotPath: string,
  goldenPath: string,
  comparison: NonNullable<Scenario["screenshot"]>,
): string | null => {
  const captured = readPixelImage(screenshotPath);
  const actual = comparison.crop === null ? captured : cropImage(captured, comparison.crop);

  if (typeof actual === "string") {
    return actual;
  }

  return findScreenshotFailure(actual, readPixelImage(goldenPath), comparison.maxDifferentPixels);
};

/**
 * A golden that does not exist yet is a note naming the picture to bless, not a failure: cage sizes the surface
 * to its own output, so the baseline has to come out of a CI artifact, and the run that produces that artifact
 * has to be green. See *Screenshots* in docs/cpp-toolchain.md.
 */
const gradeScreenshot = (inputs: GradeInputs): Grade => {
  const comparison = inputs.scenario.screenshot;

  if (comparison === null) {
    return { failures: [], notes: [] };
  }

  const goldenPath = path.join(inputs.goldensDirectory, comparison.golden);

  if (!existsSync(goldenPath)) {
    return { failures: [], notes: [`no screenshot golden at ${goldenPath}; bless ${inputs.screenshotPath}`] };
  }

  if (!existsSync(inputs.screenshotPath)) {
    return { failures: [`the window captured no screenshot at ${inputs.screenshotPath}`], notes: [] };
  }

  const failure = compareScreenshot(inputs.screenshotPath, goldenPath, comparison);

  return {
    failures: failure === null ? [] : [`the screenshot does not match ${comparison.golden}: ${failure}`],
    notes: [],
  };
};

const gradeArtifacts = (inputs: GradeInputs): Grade => {
  const grades = [gradeFrameTiming(inputs.scenario, inputs.frameLogPath), gradeScreenshot(inputs)];

  return {
    failures: grades.flatMap((grade) => grade.failures),
    notes: grades.flatMap((grade) => grade.notes),
  };
};

export { gradeArtifacts };
