import path from "node:path";

const DEFAULT_FRAME_COUNT = 600;
const MINIMUM_FRAME_COUNT = 1;
const EMPTY_LENGTH = 0;
const NOT_FOUND_INDEX = -1;
const FIRST_LINE_INDEX = 0;
const NEXT_LINE = 1;

const SCREENSHOT_FILE_NAME = "screenshot.png";
const TRACE_FILE_NAME = "trace.log";

interface Scenario {
  /** A file name under `packages/core/test-bundles`. */
  readonly bundle: string;
  /** Trace substrings the run has to produce, in this order. */
  readonly expect: readonly string[];
  /** How long `rnl_window` runs before it captures its screenshot and exits. */
  readonly frames: number;
  readonly name: string;
  /** The trace line that means the bundle has committed and input can start. */
  readonly ready: string;
  /** `rnl_inject` script lines. */
  readonly steps: readonly string[];
}

interface ArtifactPaths {
  readonly directory: string;
  readonly screenshotPath: string;
  readonly tracePath: string;
}

const isRecord = (value: unknown): value is Record<string, unknown> =>
  typeof value === "object" && value !== null && !Array.isArray(value);

const isStringArray = (value: unknown): value is readonly string[] =>
  Array.isArray(value) && value.every((entry) => typeof entry === "string");

const readString = (record: Record<string, unknown>, key: string, sourceName: string): string => {
  const value = record[key];

  if (typeof value !== "string" || value === "") {
    throw new Error(`${sourceName}: "${key}" must be a non-empty string`);
  }

  return value;
};

const readStringArray = (record: Record<string, unknown>, key: string, sourceName: string): readonly string[] => {
  const value = record[key];

  if (!isStringArray(value) || value.length === EMPTY_LENGTH) {
    throw new Error(`${sourceName}: "${key}" must be a non-empty array of strings`);
  }

  return value;
};

const readFrameCount = (record: Record<string, unknown>, sourceName: string): number => {
  const value = record["frames"];

  if (!("frames" in record)) {
    return DEFAULT_FRAME_COUNT;
  }

  if (typeof value !== "number" || !Number.isInteger(value) || value < MINIMUM_FRAME_COUNT) {
    throw new Error(`${sourceName}: "frames" must be a positive integer`);
  }

  return value;
};

const parseScenario = (value: unknown, sourceName: string): Scenario => {
  if (!isRecord(value)) {
    throw new Error(`${sourceName}: a scenario must be a JSON object`);
  }

  return {
    bundle: readString(value, "bundle", sourceName),
    expect: readStringArray(value, "expect", sourceName),
    frames: readFrameCount(value, sourceName),
    name: readString(value, "name", sourceName),
    ready: readString(value, "ready", sourceName),
    steps: readStringArray(value, "steps", sourceName),
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
    screenshotPath: path.join(directory, SCREENSHOT_FILE_NAME),
    tracePath: path.join(directory, TRACE_FILE_NAME),
  };
};

export { findMissingExpectations, formatInjectorScript, parseScenario, resolveArtifactPaths };
