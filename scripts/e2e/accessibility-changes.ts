import { isRecord, readObject, readOptionalBoolean, readString } from "./fields.ts";

import { canonicalJson } from "./snapshot.ts";

const EMPTY_LENGTH = 0;
const COMMAND_TIMEOUT_MS = 10_000;

/**
 * One entry of a scenario's `automation.accessibilityChanges`: the node a bundle's own timer is expected to
 * toggle, named by its `testID` because that is what a bundle author writes, not the tag Fabric numbered it.
 * `state`/`value` default to `false`, matching `ListAccessibilityChanges`' own carries-something convention.
 */
interface AccessibilityChangeExpectation {
  readonly state: boolean;
  readonly testID: string;
  readonly value: boolean;
}

interface ComparableAccessibilityChange {
  readonly state: boolean;
  readonly testID: string | null;
  readonly value: boolean;
}

interface AccessibilityChangesAnswer {
  readonly failure: string | null;
  readonly result: Record<string, unknown> | null;
}

type RequestAccessibilityChanges = (
  socketPath: string,
  request: { readonly command: string },
  timeoutMilliseconds: number,
) => Promise<AccessibilityChangesAnswer>;

const readAccessibilityChangeExpectation = (
  value: unknown,
  index: number,
  sourceName: string,
): AccessibilityChangeExpectation => {
  const label = `automation.accessibilityChanges[${String(index)}]`;
  const entry = readObject(value, label, sourceName);

  return {
    state: readOptionalBoolean(entry, "state", sourceName),
    testID: readString(entry["testID"], `${label}.testID`, sourceName),
    value: readOptionalBoolean(entry, "value", sourceName),
  };
};

/** A file name under the goldens directory is not involved: every entry is inline in the scenario itself. */
const readAccessibilityChanges = (
  automation: Record<string, unknown>,
  sourceName: string,
): readonly AccessibilityChangeExpectation[] | null => {
  if (!("accessibilityChanges" in automation)) {
    return null;
  }

  const changes = automation["accessibilityChanges"];

  if (!Array.isArray(changes) || changes.length === EMPTY_LENGTH) {
    throw new Error(`${sourceName}: "automation.accessibilityChanges" must be a non-empty array`);
  }

  return changes.map((change, index) => readAccessibilityChangeExpectation(change, index, sourceName));
};

const findAccessibilityChanges = (result: Record<string, unknown>): readonly unknown[] | null => {
  const { changes } = result;

  return Array.isArray(changes) ? changes : null;
};

const comparableChange = (state: boolean, testID: string | null, value: boolean): ComparableAccessibilityChange => ({
  state,
  testID,
  value,
});

const observedChange = (change: unknown): ComparableAccessibilityChange =>
  comparableChange(
    isRecord(change) && change["state"] === true,
    isRecord(change) && typeof change["testID"] === "string" ? change["testID"] : null,
    isRecord(change) && change["value"] === true,
  );

const expectedChange = (expectation: AccessibilityChangeExpectation): ComparableAccessibilityChange =>
  comparableChange(expectation.state, expectation.testID, expectation.value);

const describeChangesAnswer = (
  answer: AccessibilityChangesAnswer,
): { readonly changes: readonly unknown[] | null; readonly failures: readonly string[] } => {
  if (answer.result === null) {
    return { changes: null, failures: [String(answer.failure)] };
  }

  const changes = findAccessibilityChanges(answer.result);

  return changes === null
    ? {
        changes: null,
        failures: [`ListAccessibilityChanges answered without a "changes" list: ${JSON.stringify(answer.result)}`],
      }
    : { changes, failures: [] };
};

/**
 * The assertion of #264: an `accessibilityState`/`accessibilityValue` change has to reach `ListAccessibilityChanges`
 * named by the scenario's own `testID`, not the tag Fabric numbered it — the comparison drops `tag` from what the
 * channel answered rather than asking a scenario to guess it. The AT-SPI bridge (#27) does not exist yet; this is
 * graded on the channel carrying the change, not on bus traffic.
 */
const gradeAccessibilityChanges = async (
  requestAutomation: RequestAccessibilityChanges,
  socketPath: string,
  expectations: readonly AccessibilityChangeExpectation[] | null,
): Promise<readonly string[]> => {
  if (expectations === null) {
    return [];
  }

  const answer = await requestAutomation(socketPath, { command: "ListAccessibilityChanges" }, COMMAND_TIMEOUT_MS);
  const { changes, failures } = describeChangesAnswer(answer);

  if (changes === null) {
    return failures;
  }

  const observed = changes.map((change) => observedChange(change));
  const expected = expectations.map((expectation) => expectedChange(expectation));

  return canonicalJson(observed) === canonicalJson(expected)
    ? []
    : [
        `accessibilityChanges does not match: observed ${JSON.stringify(observed)}, expected ${JSON.stringify(expected)}`,
      ];
};

export { findAccessibilityChanges, gradeAccessibilityChanges, readAccessibilityChanges };
