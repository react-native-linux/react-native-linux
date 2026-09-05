import { cropImage, findScreenshotFailure } from "./screenshot.ts";
import { describeFrameTiming, findFrameBudgetFailures, parseFrameLogSummary } from "./frame-log.ts";
import { existsSync, readFileSync, writeFileSync } from "node:fs";

import { PNG } from "pngjs";
import type { Scenario } from "./scenario.ts";
import { gradeAutomation } from "./automation.ts";
import path from "node:path";

const NO_TEXT = "";
const CROPPED_ARTIFACT_NAME = "screenshot-cropped.png";

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

interface AutomationChannelInputs {
  readonly artifactsDirectory: string;
  readonly goldensDirectory: string;
  readonly scenario: Scenario;
  readonly trace: string;
}

interface PixelImage {
  readonly data: Uint8Array;
  readonly height: number;
  readonly width: number;
}

const readPixelImage = (imagePath: string): PixelImage => {
  const decoded = PNG.sync.read(readFileSync(imagePath));

  return { data: decoded.data, height: decoded.height, width: decoded.width };
};

const writePixelImage = (imagePath: string, image: PixelImage): void => {
  const encoded = new PNG({ height: image.height, width: image.width });

  encoded.data.set(image.data);
  writeFileSync(imagePath, PNG.sync.write(encoded));
};

const resolveCroppedArtifactPath = (screenshotPath: string): string =>
  path.join(path.dirname(screenshotPath), CROPPED_ARTIFACT_NAME);

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

interface CapturedScreenshot {
  readonly image: PixelImage;
  readonly path: string;
}

/**
 * Narrows the captured screenshot to the scenario's crop rectangle, if it names one, and writes it beside the
 * full capture as `screenshot-cropped.png` — the picture a golden gets blessed from is then already the right
 * size, and the full capture stays untouched for debugging the rest of the page. A crop that does not fit inside
 * the capture is reported the same way a size or pixel mismatch is: as a failure naming the golden, not a thrown
 * error. See *Screenshots* in docs/cpp-toolchain.md.
 */
const resolveCapturedScreenshot = (
  screenshotPath: string,
  crop: NonNullable<Scenario["screenshot"]>["crop"],
): CapturedScreenshot | string => {
  const captured = readPixelImage(screenshotPath);

  if (crop === null) {
    return { image: captured, path: screenshotPath };
  }

  const cropped = cropImage(captured, crop);

  if (typeof cropped === "string") {
    return cropped;
  }

  const croppedPath = resolveCroppedArtifactPath(screenshotPath);

  writePixelImage(croppedPath, cropped);

  return { image: cropped, path: croppedPath };
};

/**
 * A golden that does not exist yet is a note naming the picture to bless, not a failure: cage sizes the surface
 * to its own output, so the baseline has to come out of a CI artifact, and the run that produces that artifact
 * has to be green. See *Screenshots* in docs/cpp-toolchain.md.
 */
const compareAgainstGolden = (
  comparison: NonNullable<Scenario["screenshot"]>,
  goldensDirectory: string,
  captured: CapturedScreenshot,
): Grade => {
  const goldenPath = path.join(goldensDirectory, comparison.golden);

  if (!existsSync(goldenPath)) {
    return { failures: [], notes: [`no screenshot golden at ${goldenPath}; bless ${captured.path}`] };
  }

  const failure = findScreenshotFailure(captured.image, readPixelImage(goldenPath), comparison.maxDifferentPixels);

  return {
    failures: failure === null ? [] : [`the screenshot does not match ${comparison.golden}: ${failure}`],
    notes: [],
  };
};

const gradeScreenshot = (inputs: GradeInputs): Grade => {
  const comparison = inputs.scenario.screenshot;

  if (comparison === null) {
    return { failures: [], notes: [] };
  }

  if (!existsSync(inputs.screenshotPath)) {
    return { failures: [`the window captured no screenshot at ${inputs.screenshotPath}`], notes: [] };
  }

  const captured = resolveCapturedScreenshot(inputs.screenshotPath, comparison.crop);

  if (typeof captured === "string") {
    return { failures: [`the screenshot does not match ${comparison.golden}: ${captured}`], notes: [] };
  }

  return compareAgainstGolden(comparison, inputs.goldensDirectory, captured);
};

const gradeArtifacts = (inputs: GradeInputs): Grade => {
  const grades = [gradeFrameTiming(inputs.scenario, inputs.frameLogPath), gradeScreenshot(inputs)];

  return {
    failures: grades.flatMap((grade) => grade.failures),
    notes: grades.flatMap((grade) => grade.notes),
  };
};

/**
 * The automation channel of #214, asked while the window is still up — which is why this is the one grader the
 * driver calls before the run ends rather than after it. A scenario without an `automation` block never opened
 * the socket, so there is nothing to ask.
 */
const gradeAutomationChannel = (inputs: AutomationChannelInputs): Promise<readonly string[]> =>
  inputs.scenario.automation === null
    ? Promise.resolve([])
    : gradeAutomation({
        artifactsDirectory: inputs.artifactsDirectory,
        automation: inputs.scenario.automation,
        goldensDirectory: inputs.goldensDirectory,
        trace: inputs.trace,
      });

export { gradeArtifacts, gradeAutomationChannel };
