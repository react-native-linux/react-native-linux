import { SUITE_ERROR_KEY, gradeFantomRuns, readExpectations } from "./fantom-expectations.ts";
import { describe, expect, it } from "vitest";

const suite = "private/timers/__tests__/Timers-itest.js";
const reason = "NativeFantom.setTimerMockEnabled is not implemented (#210)";
const results = (...testResults: readonly { fullName: string; status: string }[]): unknown => ({ testResults });

describe("readExpectations", () => {
  it("reads each suite's expected failures and their reasons", () => {
    expect(readExpectations({ [suite]: { "fires once": reason } })).toStrictEqual({
      [suite]: { "fires once": reason },
    });
  });

  it.each([[null], [[]]])("refuses expectations that are not an object of suites: %j", (contents) => {
    expect(() => readExpectations(contents)).toThrow("the Fantom expectations must be an object of suites");
  });

  it.each([[[]], [{ "fires once": 1 }]])("refuses a suite whose failures are not reasons: %j", (failures) => {
    expect(() => readExpectations({ [suite]: failures })).toThrow(`${suite} must map each expected failure`);
  });
});

describe("gradeFantomRuns", () => {
  it("accepts passes, expected failures and upstream's own skipped tests", () => {
    const run = {
      result: results(
        { fullName: "passes", status: "passed" },
        { fullName: "fires once", status: "failed" },
        { fullName: "skipped upstream", status: "pending" },
      ),
      suite,
    };

    expect(gradeFantomRuns([run], { [suite]: { "fires once": reason } })).toStrictEqual([]);
  });

  it("names a new failure, a listed failure that passes now, and a listed one that went unreported", () => {
    const run = {
      result: results({ fullName: "regressed", status: "failed" }, { fullName: "fixed", status: "passed" }),
      suite,
    };

    expect(gradeFantomRuns([run], { [suite]: { fixed: reason, vanished: reason } })).toStrictEqual([
      `${suite}: "regressed" failed`,
      `${suite}: "fixed" passes now; remove its expectation`,
      `${suite}: "vanished" is expected but was not reported`,
    ]);
  });

  it("ignores malformed test results", () => {
    const run = { result: { testResults: [null, { fullName: 1 }, { fullName: "x", status: "failed" }] }, suite };

    expect(gradeFantomRuns([run], {})).toStrictEqual([`${suite}: "x" failed`]);
  });

  it("names a suite that failed before running, unless that is what it is expected to do", () => {
    const run = { result: { error: { message: "boom" } }, suite };

    expect(gradeFantomRuns([run], {})).toStrictEqual([
      `${suite}: the suite failed before running: {"error":{"message":"boom"}}`,
    ]);
    expect(gradeFantomRuns([run], { [suite]: { [SUITE_ERROR_KEY]: reason } })).toStrictEqual([]);
    expect(gradeFantomRuns([{ result: null, suite }], {})).toStrictEqual([
      `${suite}: the suite failed before running: null`,
    ]);
  });
});
