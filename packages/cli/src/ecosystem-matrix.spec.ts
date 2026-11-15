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

describe("T3 Code's rows in the ledger (#635)", () => {
  const ledgerStatesByStatus = new Map([
    ["tracked", ["in progress", "blocked"]],
    ["declined", ["declined"]],
  ]);
  const researchNote = readFileSync(path.join(repositoryRoot, "docs", "research", "t3code-linux.md"), "utf8");
  const ledgerSection = researchNote.slice(researchNote.indexOf("## 4. The ledger"));
  const ledgerTableStart = ledgerSection.indexOf("\n|");
  const ledgerTable = ledgerSection.slice(ledgerTableStart, ledgerSection.indexOf("\n\n", ledgerTableStart));
  const noteRows = [
    ...ledgerTable.matchAll(/^\| `(?<packageName>[^`]+)` \| `[^`]+` \| (?<status>[a-z-]+) \| (?<owners>[^|]+) \|/gmu),
  ].map(({ groups }) => ({
    owners: (groups?.["owners"] ?? "").trim().split(", "),
    packageName: groups?.["packageName"] ?? "",
    status: groups?.["status"] ?? "",
  }));

  it("reads every package row of the research note", () => {
    const packageLines = ledgerTable.split("\n").filter((line) => line.startsWith("| `"));

    expect(noteRows).not.toStrictEqual([]);
    expect(noteRows).toHaveLength(packageLines.length);
  });

  it("agrees with the research note on each package's state and owner", () => {
    const disagreements = noteRows.flatMap(({ owners, packageName, status }) => {
      const row = ecosystemMatrix.find((candidate) => candidate.packageName === packageName);

      if (!row) {
        return [packageName];
      }

      const owner = "issue" in row ? `#${row.issue}` : `\`${row.test}\``;

      return owners.includes(owner) && ledgerStatesByStatus.get(status)?.includes(row.state) ? [] : [packageName];
    });

    expect(disagreements).toStrictEqual([]);
  });
});
