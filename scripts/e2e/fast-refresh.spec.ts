import { applyFastRefreshEdit, gradeEditToVisible, readFastRefresh } from "./fast-refresh.ts";
import { describe, expect, it } from "vitest";

const block = {
  expect: "fast-refresh: title After",
  file: "packages/test-harness/src/fast-refresh-app.tsx",
  find: '"Before"',
  maxEditToVisibleMs: 5000,
  replace: '"After"',
};

const WITHIN_BUDGET_MS = 1200;
const OVER_BUDGET_MS = 6000;

const edit = readFastRefresh({ fastRefresh: block }, "fixture.json");

describe("readFastRefresh", () => {
  it("is null when the scenario has no fastRefresh block", () => {
    expect(readFastRefresh({}, "fixture.json")).toBeNull();
  });

  it("reads every field", () => {
    expect(edit).toStrictEqual(block);
  });

  it.each([
    ["an absolute path", "/etc/passwd"],
    ["a parent segment", "../outside.tsx"],
  ])("refuses %s as the edited file", (_description, file) => {
    expect(() => readFastRefresh({ fastRefresh: { ...block, file } }, "fixture.json")).toThrow(
      'fixture.json: "fastRefresh.file" must be a relative path inside the repository',
    );
  });
});

describe("applyFastRefreshEdit", () => {
  it("replaces the text it names", () => {
    expect(edit === null ? null : applyFastRefreshEdit('const title = "Before";', edit)).toBe('const title = "After";');
  });

  it("names the file when the text is not there", () => {
    expect(() => (edit === null ? null : applyFastRefreshEdit("const title = 1;", edit))).toThrow(
      `packages/test-harness/src/fast-refresh-app.tsx does not contain "\\"Before\\""`,
    );
  });
});

describe("gradeEditToVisible", () => {
  it("passes inside the budget and records the number", () => {
    expect(edit === null ? null : gradeEditToVisible(WITHIN_BUDGET_MS, edit)).toStrictEqual({
      failures: [],
      note: "fast refresh: edit-to-visible 1200 ms",
    });
  });

  it("fails over the budget", () => {
    expect(edit === null ? null : gradeEditToVisible(OVER_BUDGET_MS, edit).failures).toStrictEqual([
      "fast refresh took 6000 ms from edit to visible, the budget is 5000 ms",
    ]);
  });

  it("fails when the edit never became visible", () => {
    expect(edit === null ? null : gradeEditToVisible(null, edit)).toStrictEqual({
      failures: [
        '"fast-refresh: title After" never appeared after editing packages/test-harness/src/fast-refresh-app.tsx',
      ],
      note: "fast refresh: the edit never became visible",
    });
  });
});
