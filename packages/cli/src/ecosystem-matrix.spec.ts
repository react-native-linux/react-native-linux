import { describe, expect, it } from "vitest";
import { ecosystemMatrix } from "./ecosystem-matrix.ts";
import { existsSync } from "node:fs";
import path from "node:path";

const repositoryRoot = path.join(import.meta.dirname, "..", "..", "..");
const FIRST_ISSUE_NUMBER = 1;

describe("the flagship's native dependency ledger (#87)", () => {
  it("names each dependency once", () => {
    const packageNames = ecosystemMatrix.map((row) => row.packageName);

    expect(new Set(packageNames).size).toBe(packageNames.length);
  });

  it("backs every works and shimmed row with a test that exists", () => {
    const missingTests = ecosystemMatrix.flatMap((row) =>
      (row.state === "works" || row.state === "shimmed") && !existsSync(path.join(repositoryRoot, row.test))
        ? [`${row.packageName}: ${row.test}`]
        : [],
    );

    expect(missingTests).toStrictEqual([]);
  });

  it("names an owning issue for every row still in progress or blocked", () => {
    const withoutIssue = ecosystemMatrix.flatMap((row) =>
      (row.state === "in progress" || row.state === "blocked") &&
      !(Number.isInteger(row.issue) && row.issue >= FIRST_ISSUE_NUMBER)
        ? [row.packageName]
        : [],
    );

    expect(withoutIssue).toStrictEqual([]);
  });
});
