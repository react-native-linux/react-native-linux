import type { Scenario } from "./scenario.ts";

const EMPTY_LENGTH = 0;
const NOT_FOUND_INDEX = -1;
const FIRST_LINE_INDEX = 0;
const NEXT_LINE = 1;

/**
 * The narrow slice of "error" the trace can prove today, per #233: an uncaught JS error's own report from
 * `JsErrorReporter`, and the bracketed component tags `rnl_window`'s C++ diagnostics use when they hit a fault.
 * A raw `console.error`/`console.warn` call is deliberately not in this list — `ConsoleBinding` prints it with no
 * prefix, indistinguishable from `console.log`, until #214's `ListErrors` channel replaces this mechanism.
 */
const ERROR_TRACE_PATTERNS: readonly string[] = ["[js-error]", "[bundle-runner]", "[image]", "[text]", "[rnl-window]"];

/** Ordered substring matching: every expectation must appear on a later line than the one before it. */
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

/** Every `reject` substring that appears anywhere in the trace, checked regardless of `allowErrors` or `expectFailure`. */
const findRejectedMatches = (traceLines: readonly string[], rejections: readonly string[]): readonly string[] =>
  rejections.filter((rejection) => traceLines.some((line) => line.includes(rejection)));

/**
 * Every failure the trace itself proves that `expectFailure` may legitimately invert: a missing expectation, and
 * — unless `allowErrors` opts a scenario out — a logged error line. `reject` deliberately does not feed this: see
 * `describeRejectedTraceFailures`.
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
 * Every `reject` substring the trace produced, as a failure `resolveExpectedOutcome` never sees and therefore can
 * never invert: `expectFailure` exists to pass a negative control that produced the one failure it was built to
 * produce, and a rejected substring is never that failure — it means something else went wrong in the same run,
 * which stays a failure even when the scenario also expects a different one.
 */
const describeRejectedTraceFailures = (scenario: Scenario, trace: string): readonly string[] =>
  findRejectedMatches(trace.split("\n"), scenario.reject).map(
    (rejection) => `the trace produced the rejected "${rejection}"`,
  );

/** `compositor.signalCode` set means `cage`/`rnl_window` crashed: never a scenario's own exit, never invertible. */
const describeCompositorCrash = (signalCode: NodeJS.Signals | null): readonly string[] =>
  signalCode === null ? [] : [`cage signal ${signalCode}`];

/** The run-level facts `resolveExpectedOutcome` needs beyond the scenario and its failures, bundled to stay at 3 params. */
interface RunOutcome {
  readonly signal: NodeJS.Signals | null;
  readonly trace: string;
}

/**
 * `expectFailure` inverts `failures` for a negative control: the scenario passes only when grading it produced at
 * least one, and reports one of its own when it produced none. `outcome.trace`'s rejected substrings and
 * `outcome.signal`'s crash are appended after that inversion rather than folded into `failures` before it, on
 * purpose: neither is ever the one failure `expectFailure` expects, so both must go on keeping the scenario
 * failing even once the inversion clears everything else.
 */
const resolveExpectedOutcome = (
  scenario: Scenario,
  failures: readonly string[],
  outcome: RunOutcome,
): readonly string[] => {
  const unwaivable = [
    ...describeCompositorCrash(outcome.signal),
    ...describeRejectedTraceFailures(scenario, outcome.trace),
  ];

  if (!scenario.expectFailure) {
    return [...unwaivable, ...failures];
  }

  const inverted =
    failures.length === EMPTY_LENGTH ? ["expectFailure is set, but the scenario produced no failures"] : [];

  return [...unwaivable, ...inverted];
};

export {
  describeCompositorCrash,
  describeRejectedTraceFailures,
  describeTraceFailures,
  findErrorLines,
  findMissingExpectations,
  findRejectedMatches,
  resolveExpectedOutcome,
};
