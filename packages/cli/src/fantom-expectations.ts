/**
 * #210/#423: what an upstream itest run through Fantom is expected to report. The expectations file is the corpus
 * itself: every suite it names is run, and every test it lists is a known failure with the issue that owns it. A
 * run is clean when nothing outside the list fails, nothing on the list passes, and nothing on the list went
 * unreported, so a fixed gap and a new one both turn into a reviewed edit of the file.
 */
type SuiteExpectations = Readonly<Record<string, string>>;

interface FantomTestResult {
  readonly fullName: string;
  readonly status: string;
}

interface FantomSuiteRun {
  readonly result: unknown;
  readonly suite: string;
}

/** The key under which a suite that fails before running any test is expected to. */
const SUITE_ERROR_KEY = "(suite)";
const PASSED = "passed";
const FAILED = "failed";

const isRecord = (value: unknown): value is Record<string, unknown> =>
  typeof value === "object" && value !== null && !Array.isArray(value);

const readExpectations = (contents: unknown): Readonly<Record<string, SuiteExpectations>> => {
  if (!isRecord(contents)) {
    throw new TypeError("the Fantom expectations must be an object of suites");
  }

  return Object.fromEntries(
    Object.entries(contents).map(([suite, failures]) => {
      if (!isRecord(failures) || !Object.values(failures).every((reason) => typeof reason === "string")) {
        throw new TypeError(`${suite} must map each expected failure to its reason`);
      }

      return [suite, Object.fromEntries(Object.entries(failures).map(([name, reason]) => [name, String(reason)]))];
    }),
  );
};

const readTestResults = (result: unknown): readonly FantomTestResult[] | null => {
  const testResults = isRecord(result) ? result["testResults"] : null;

  return Array.isArray(testResults)
    ? testResults.filter(
        (test): test is FantomTestResult =>
          isRecord(test) && typeof test["fullName"] === "string" && typeof test["status"] === "string",
      )
    : null;
};

const gradeSuite = ({ result, suite }: FantomSuiteRun, expected: SuiteExpectations): readonly string[] => {
  const testResults = readTestResults(result);

  if (testResults === null) {
    return SUITE_ERROR_KEY in expected ? [] : [`${suite}: the suite failed before running: ${JSON.stringify(result)}`];
  }

  const reported = new Set(testResults.map(({ fullName }) => fullName));

  return [
    ...testResults.flatMap(({ fullName, status }) => {
      const isExpectedToFail = fullName in expected;

      if (status === PASSED && isExpectedToFail) {
        return [`${suite}: "${fullName}" passes now; remove its expectation`];
      }

      // Upstream's own `it.skip` reports as pending: neither a pass nor a failure.
      return status === FAILED && !isExpectedToFail ? [`${suite}: "${fullName}" failed`] : [];
    }),
    ...Object.keys(expected)
      .filter((fullName) => !reported.has(fullName))
      .map((fullName) => `${suite}: "${fullName}" is expected but was not reported`),
  ];
};

const gradeFantomRuns = (
  runs: readonly FantomSuiteRun[],
  expectations: Readonly<Record<string, SuiteExpectations>>,
): readonly string[] => runs.flatMap((run) => gradeSuite(run, expectations[run.suite] ?? {}));

export { gradeFantomRuns, readExpectations, SUITE_ERROR_KEY };
