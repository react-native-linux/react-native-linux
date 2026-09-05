import {
  isRecord,
  readCoordinate,
  readObject,
  readOptionalBoolean,
  readPositiveInteger,
  readPositiveNumber,
  readString,
  readStringArray,
} from "./fields.ts";

import type { Crop } from "./screenshot.ts";
import path from "node:path";

const DEFAULT_FRAME_COUNT = 600;
const EMPTY_LENGTH = 0;
const NOT_FOUND_INDEX = -1;
const FIRST_LINE_INDEX = 0;
const NEXT_LINE = 1;

const FRAME_LOG_FILE_NAME = "frames.jsonl";
const SCREENSHOT_FILE_NAME = "screenshot.png";
const TRACE_FILE_NAME = "trace.log";

/**
 * The narrow slice of "error" the trace can prove today, per #233: an uncaught JS error's own report from
 * `JsErrorReporter`, and the bracketed component tags `rnl_window`'s C++ diagnostics use when they hit a fault.
 * A raw `console.error`/`console.warn` call is deliberately not in this list — `ConsoleBinding` prints it with
 * no prefix at all, so nothing in the merged stdout/stderr trace tells it apart from `console.log` until #214's
 * `ListErrors` channel replaces this trace-substring mechanism.
 */
const ERROR_TRACE_PATTERNS: readonly string[] = ["[js-error]", "[bundle-runner]", "[image]", "[text]", "[rnl-window]"];

/**
 * The perf gate of #7. `p95Ms` is the ninety-fifth percentile `wp_presentation` frame time the run may not
 * exceed, and `minFrames` is how many frames have to have been presented for that percentile to mean anything —
 * a run that presented four frames can pass any budget by accident.
 */
interface FrameBudget {
  readonly minFrames: number;
  readonly p95Ms: number;
}

/**
 * `golden` is a file name under `packages/core/e2e/goldens`. A golden that does not exist yet is a skip with a
 * note rather than a failure, because the picture has to be blessed from a CI artifact before it can be compared.
 *
 * `crop`, when set, narrows the captured screenshot to a rectangle before comparing it against the golden, so an
 * unrelated change elsewhere on the page does not invalidate it. The golden is stored already cropped to that
 * same rectangle — addressing the rectangle by a node's `testID` instead needs #216's tree dump and is out of
 * scope here, so the scenario names it directly.
 */
interface ScreenshotComparison {
  readonly crop: Crop | null;
  readonly golden: string;
  readonly maxDifferentPixels: number;
}

/**
 * What the automation channel (#214) is asked to prove about this scenario, and the flag that opens it: a
 * scenario without an `automation` block runs a window that never listens. `listErrorsMustBeEmpty` is the
 * `verifyNoErrorLogs` react-native-windows asserts in `afterEach`, asked of the runtime rather than grepped out
 * of the trace; `visualTreeSnapshot` names a file under the package's `e2e/goldens` the committed tree has to
 * match; `accessibilityTreeSnapshot` names one under `e2e/snapshots` its accessibility projection has to match;
 * `markTestPassed` requires the bundle to have called `globalThis.__rnlMarkTestPassed()`.
 */
interface ScenarioAutomation {
  readonly accessibilityTreeSnapshot: string | null;
  readonly listErrorsMustBeEmpty: boolean;
  readonly markTestPassed: boolean;
  readonly visualTreeSnapshot: string | null;
}

interface Scenario {
  /** Lets a scenario that knowingly logs an error, such as a negative control, skip the #233 error gate. */
  readonly allowErrors: boolean;
  readonly automation: ScenarioAutomation | null;
  /** A file name under `packages/core/test-bundles`. */
  readonly bundle: string;
  /** Trace substrings the run has to produce, in this order. */
  readonly expect: readonly string[];
  /** A negative control: the scenario passes only if grading it produces at least one failure. */
  readonly expectFailure: boolean;
  /** How long `rnl_window` runs before it captures its screenshot and exits. */
  readonly frames: number;
  readonly frameBudget: FrameBudget | null;
  readonly name: string;
  /** The trace line that means the bundle has committed and input can start. */
  readonly ready: string;
  readonly screenshot: ScreenshotComparison | null;
  /** `rnl_inject` script lines. */
  readonly steps: readonly string[];
}

interface ArtifactPaths {
  readonly directory: string;
  readonly frameLogPath: string;
  readonly screenshotPath: string;
  readonly tracePath: string;
}

const readFrameCount = (record: Record<string, unknown>, sourceName: string): number => {
  if (!("frames" in record)) {
    return DEFAULT_FRAME_COUNT;
  }

  return readPositiveInteger(record["frames"], "frames", sourceName);
};

const readFrameBudget = (record: Record<string, unknown>, sourceName: string): FrameBudget | null => {
  if (!("frameBudget" in record)) {
    return null;
  }

  const budget = readObject(record["frameBudget"], "frameBudget", sourceName);

  return {
    minFrames: readPositiveInteger(budget["minFrames"], "frameBudget.minFrames", sourceName),
    p95Ms: readPositiveNumber(budget["p95Ms"], "frameBudget.p95Ms", sourceName),
  };
};

const readCrop = (comparison: Record<string, unknown>, sourceName: string): Crop | null => {
  if (!("crop" in comparison)) {
    return null;
  }

  const crop = readObject(comparison["crop"], "screenshot.crop", sourceName);

  return {
    height: readPositiveInteger(crop["height"], "screenshot.crop.height", sourceName),
    left: readCoordinate(crop["left"], "screenshot.crop.left", sourceName),
    top: readCoordinate(crop["top"], "screenshot.crop.top", sourceName),
    width: readPositiveInteger(crop["width"], "screenshot.crop.width", sourceName),
  };
};

const readScreenshotComparison = (record: Record<string, unknown>, sourceName: string): ScreenshotComparison | null => {
  if (!("screenshot" in record)) {
    return null;
  }

  const comparison = readObject(record["screenshot"], "screenshot", sourceName);

  return {
    crop: readCrop(comparison, sourceName),
    golden: readString(comparison["golden"], "screenshot.golden", sourceName),
    maxDifferentPixels: readPositiveInteger(
      comparison["maxDifferentPixels"],
      "screenshot.maxDifferentPixels",
      sourceName,
    ),
  };
};

const readSnapshotName = (automation: Record<string, unknown>, field: string, sourceName: string): string | null =>
  field in automation ? readString(automation[field], `automation.${field}`, sourceName) : null;

const readAutomation = (record: Record<string, unknown>, sourceName: string): ScenarioAutomation | null => {
  if (!("automation" in record)) {
    return null;
  }

  const automation = readObject(record["automation"], "automation", sourceName);

  return {
    accessibilityTreeSnapshot: readSnapshotName(automation, "accessibilityTreeSnapshot", sourceName),
    listErrorsMustBeEmpty: readOptionalBoolean(automation, "listErrorsMustBeEmpty", sourceName),
    markTestPassed: readOptionalBoolean(automation, "markTestPassed", sourceName),
    visualTreeSnapshot: readSnapshotName(automation, "visualTreeSnapshot", sourceName),
  };
};

const parseScenario = (value: unknown, sourceName: string): Scenario => {
  if (!isRecord(value)) {
    throw new Error(`${sourceName}: a scenario must be a JSON object`);
  }

  return {
    allowErrors: readOptionalBoolean(value, "allowErrors", sourceName),
    automation: readAutomation(value, sourceName),
    bundle: readString(value["bundle"], "bundle", sourceName),
    expect: readStringArray(value["expect"], "expect", sourceName),
    expectFailure: readOptionalBoolean(value, "expectFailure", sourceName),
    frameBudget: readFrameBudget(value, sourceName),
    frames: readFrameCount(value, sourceName),
    name: readString(value["name"], "name", sourceName),
    ready: readString(value["ready"], "ready", sourceName),
    screenshot: readScreenshotComparison(value, sourceName),
    steps: readStringArray(value["steps"], "steps", sourceName),
  };
};

const formatInjectorScript = (steps: readonly string[]): string => `${steps.join("\n")}\n`;

/**
 * Ordered substring matching: every expectation has to appear on a later line than the one before it, which is
 * what makes a trace assertion about a sequence of events rather than a set of them.
 */
const findMissingExpectations = (traceLines: readonly string[], expectations: readonly string[]): readonly string[] => {
  const missing: string[] = [];
  let searchIndex = FIRST_LINE_INDEX;

  for (const expectation of expectations) {
    const remaining = traceLines.slice(searchIndex);
    const matchIndex = remaining.findIndex((line) => line.includes(expectation));

    if (matchIndex === NOT_FOUND_INDEX) {
      missing.push(expectation);
    } else {
      searchIndex += matchIndex + NEXT_LINE;
    }
  }

  return missing;
};

/** Every trace line that matches one of `ERROR_TRACE_PATTERNS`, in the order the trace produced them. */
const findErrorLines = (traceLines: readonly string[]): readonly string[] =>
  traceLines.filter((line) => ERROR_TRACE_PATTERNS.some((pattern) => line.includes(pattern)));

/**
 * Every failure the trace itself proves: a missing expectation, and — unless `allowErrors` opts a scenario out —
 * a logged error line. The #233 error gate stacks onto the pre-existing ordered-substring assertions rather than
 * replacing them.
 */
const describeTraceFailures = (scenario: Scenario, trace: string): readonly string[] => {
  const traceLines = trace.split("\n");
  const missing = findMissingExpectations(traceLines, scenario.expect).map(
    (expectation) => `the trace never produced "${expectation}"`,
  );

  if (scenario.allowErrors) {
    return missing;
  }

  return [...missing, ...findErrorLines(traceLines).map((line) => `the trace logged an error: ${line}`)];
};

/**
 * `expectFailure` inverts a run's failures for a negative control: the scenario passes only when grading it
 * produced at least one failure, and reports one of its own when grading produced none.
 */
const resolveExpectedOutcome = (scenario: Scenario, failures: readonly string[]): readonly string[] => {
  if (!scenario.expectFailure) {
    return failures;
  }

  return failures.length === EMPTY_LENGTH ? ["expectFailure is set, but the scenario produced no failures"] : [];
};

const resolveArtifactPaths = (artifactsRoot: string, scenarioName: string): ArtifactPaths => {
  const directory = path.join(artifactsRoot, scenarioName);

  return {
    directory,
    frameLogPath: path.join(directory, FRAME_LOG_FILE_NAME),
    screenshotPath: path.join(directory, SCREENSHOT_FILE_NAME),
    tracePath: path.join(directory, TRACE_FILE_NAME),
  };
};

export {
  describeTraceFailures,
  findErrorLines,
  findMissingExpectations,
  formatInjectorScript,
  parseScenario,
  resolveArtifactPaths,
  resolveExpectedOutcome,
};
export type { FrameBudget, Scenario, ScenarioAutomation };
