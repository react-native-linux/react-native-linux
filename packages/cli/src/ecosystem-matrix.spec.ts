import { describe, expect, it } from "vitest";
import { existsSync, readFileSync } from "node:fs";
import { ecosystemMatrix } from "./ecosystem-matrix.ts";
import { packageAliases } from "./package-aliases.ts";
import path from "node:path";

const repositoryRoot = path.join(import.meta.dirname, "..", "..", "..");
const FIRST_ISSUE_NUMBER = 1;

describe("harness dependency coverage (#87)", () => {
  it("tracks harness runtime dependencies beyond the React Native and Babel runtime", () => {
    const manifest: unknown = JSON.parse(
      readFileSync(path.join(repositoryRoot, "packages", "test-harness", "package.json"), "utf8"),
    );

    if (typeof manifest !== "object" || manifest === null || !("dependencies" in manifest)) {
      throw new TypeError("The harness manifest must declare dependencies");
    }

    const { dependencies } = manifest;

    if (typeof dependencies !== "object" || dependencies === null) {
      throw new TypeError("The harness dependencies must be an object");
    }

    const missingRows = Object.keys(dependencies).filter((packageName) => {
      const upstream = packageAliases.find((alias) => alias.linux === packageName)?.upstream ?? packageName;

      return (
        !["react", "react-native", "@babel/runtime"].includes(packageName) &&
        !ecosystemMatrix.some((row) => row.packageName === upstream)
      );
    });

    expect(missingRows).toStrictEqual([]);
  });
});

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

  it("names an owning or decision issue for every in-progress, blocked or declined row", () => {
    const withoutIssue = ecosystemMatrix.flatMap((row) =>
      (row.state === "in progress" || row.state === "blocked" || row.state === "declined") &&
      !(Number.isInteger(row.issue) && row.issue >= FIRST_ISSUE_NUMBER)
        ? [row.packageName]
        : [],
    );

    expect(withoutIssue).toStrictEqual([]);
  });
});
