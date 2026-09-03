import path from "node:path";

const DEFAULT_FRAME_COUNT = 600;
const MINIMUM_FRAME_COUNT = 1;
const MINIMUM_POSITIVE_NUMBER = 0;
const EMPTY_LENGTH = 0;
const NOT_FOUND_INDEX = -1;
const FIRST_LINE_INDEX = 0;
const NEXT_LINE = 1;

const FRAME_LOG_FILE_NAME = "frames.jsonl";
const SCREENSHOT_FILE_NAME = "screenshot.png";
const TRACE_FILE_NAME = "trace.log";

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
 */
interface ScreenshotComparison {
  readonly golden: string;
  readonly maxDifferentPixels: number;
}

interface Scenario {
  /** A file name under `packages/core/test-bundles`. */
  readonly bundle: string;
  /** Trace substrings the run has to produce, in this order. */
  readonly expect: readonly string[];
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

const isRecord = (value: unknown): value is Record<string, unknown> =>
  typeof value === "object" && value !== null && !Array.isArray(value);

const isStringArray = (value: unknown): value is readonly string[] =>
  Array.isArray(value) && value.every((entry) => typeof entry === "string");

const readString = (value: unknown, label: string, sourceName: string): string => {
  if (typeof value !== "string" || value === "") {
    throw new Error(`${sourceName}: "${label}" must be a non-empty string`);
  }

  return value;
};

const readStringArray = (value: unknown, label: string, sourceName: string): readonly string[] => {
  if (!isStringArray(value) || value.length === EMPTY_LENGTH) {
    throw new Error(`${sourceName}: "${label}" must be a non-empty array of strings`);
  }

  return value;
};

const readPositiveInteger = (value: unknown, label: string, sourceName: string): number => {
  if (typeof value !== "number" || !Number.isInteger(value) || value < MINIMUM_FRAME_COUNT) {
    throw new Error(`${sourceName}: "${label}" must be a positive integer`);
  }

  return value;
};

const readPositiveNumber = (value: unknown, label: string, sourceName: string): number => {
  if (typeof value !== "number" || !Number.isFinite(value) || value <= MINIMUM_POSITIVE_NUMBER) {
    throw new Error(`${sourceName}: "${label}" must be a positive number`);
  }

  return value;
};

const readObject = (value: unknown, label: string, sourceName: string): Record<string, unknown> => {
  if (!isRecord(value)) {
    throw new Error(`${sourceName}: "${label}" must be a JSON object`);
  }

  return value;
};

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

const readScreenshotComparison = (record: Record<string, unknown>, sourceName: string): ScreenshotComparison | null => {
  if (!("screenshot" in record)) {
    return null;
  }

  const comparison = readObject(record["screenshot"], "screenshot", sourceName);

  return {
    golden: readString(comparison["golden"], "screenshot.golden", sourceName),
    maxDifferentPixels: readPositiveInteger(
      comparison["maxDifferentPixels"],
      "screenshot.maxDifferentPixels",
      sourceName,
    ),
  };
};

const parseScenario = (value: unknown, sourceName: string): Scenario => {
  if (!isRecord(value)) {
    throw new Error(`${sourceName}: a scenario must be a JSON object`);
  }

  return {
    bundle: readString(value["bundle"], "bundle", sourceName),
    expect: readStringArray(value["expect"], "expect", sourceName),
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

const resolveArtifactPaths = (artifactsRoot: string, scenarioName: string): ArtifactPaths => {
  const directory = path.join(artifactsRoot, scenarioName);

  return {
    directory,
    frameLogPath: path.join(directory, FRAME_LOG_FILE_NAME),
    screenshotPath: path.join(directory, SCREENSHOT_FILE_NAME),
    tracePath: path.join(directory, TRACE_FILE_NAME),
  };
};

export { findMissingExpectations, formatInjectorScript, parseScenario, resolveArtifactPaths };
export type { FrameBudget, Scenario };
