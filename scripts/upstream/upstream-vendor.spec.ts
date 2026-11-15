import {
  GREETING_PATCHED,
  GREETING_PATH,
  createUpstreamFixture,
  defaultLock,
  libraryPathsOf,
  readTree,
  removeFixture,
  resetWorkspace,
  runCommand,
  writeLock,
  writeTree,
} from "./upstream-test-fixtures.ts";
import { afterAll, beforeEach, expect, it } from "vitest";

import { readlinkSync, rmSync, symlinkSync } from "node:fs";
import type { CommandResult } from "./upstream-types.ts";
import path from "node:path";

const SUCCESS_EXIT_CODE = 0;
const FAILURE_EXIT_CODE = 1;

const fixture = createUpstreamFixture();

const joined = (result: CommandResult): string => result.messages.join("\n");

afterAll(() => {
  removeFixture(fixture);
});

beforeEach(() => {
  resetWorkspace(fixture);
});

it("vendors the locked tag with the patch queue applied and without the clone's git directory", () => {
  const result = runCommand(fixture, ["vendor", "widget"]);
  const gitDirectory = path.join(libraryPathsOf(fixture).treeDirectory, ".git");

  expect(result).toStrictEqual({
    exitCode: SUCCESS_EXIT_CODE,
    messages: ["vendored packages/widget/upstream at v1", "  upstream files: 3", "  patches applied: 1"],
  });
  expect(readTree(fixture, GREETING_PATH)).toBe(GREETING_PATCHED);
  expect(readTree(fixture, "README.md")).toBe("widget\n");
  expect(fixture.environment.pathExists(gitDirectory)).toBe(false);
});

it("copies a relative symbolic link as it is, rather than pointing it into the clone it came from", () => {
  const { environment } = fixture;
  const source = path.join(fixture.temporaryRoot, "linked");
  const copy = path.join(fixture.temporaryRoot, "linked-copy");

  environment.writeTextFile(path.join(source, "AGENTS.md"), "agents\n");
  symlinkSync("AGENTS.md", path.join(source, "CLAUDE.md"));
  environment.copyDirectory(source, copy);

  expect(readlinkSync(path.join(copy, "CLAUDE.md"))).toBe("AGENTS.md");
});

it("lists a symbolic link and hashes its target, so a retargeted link is drift", () => {
  const { environment } = fixture;
  const directory = path.join(fixture.temporaryRoot, "hashed-links");
  const link = path.join(directory, "CLAUDE.md");

  environment.writeTextFile(path.join(directory, "AGENTS.md"), "agents\n");
  symlinkSync("AGENTS.md", link);
  const digestBefore = environment.hashFile(link);
  rmSync(link);
  symlinkSync("README.md", link);

  expect(environment.listFiles(directory)).toStrictEqual(["AGENTS.md", "CLAUDE.md"]);
  expect(environment.hashFile(link)).not.toBe(digestBefore);
});

const tagAgentInstructions = (): void => {
  const { environment, originDirectory } = fixture;
  const agentFiles = ["AGENTS.md", "GEMINI.md", ".cursorrules", "src/AGENTS.md", "src/.claude/settings.json"];

  environment.runGit(["checkout", "--quiet", "-b", "agents", "v1"], originDirectory);
  for (const relativePath of agentFiles) {
    environment.writeTextFile(path.join(originDirectory, relativePath), "instructions\n");
  }
  symlinkSync("AGENTS.md", path.join(originDirectory, "CLAUDE.md"));
  environment.runGit(["add", "--all"], originDirectory);
  environment.runGit(["commit", "--quiet", "--message", "agents"], originDirectory);
  environment.runGit(["tag", "agents"], originDirectory);
  environment.runGit(["checkout", "--quiet", "main"], originDirectory);
};

it("leaves agent instruction files out of the vendored tree and its lock, at the root and below it", () => {
  const { environment } = fixture;

  tagAgentInstructions();

  expect(runCommand(fixture, ["bump", "widget", "agents"]).exitCode).toBe(SUCCESS_EXIT_CODE);
  expect(environment.listFiles(libraryPathsOf(fixture).treeDirectory)).toStrictEqual([
    "README.md",
    "src/greeting.ts",
    "src/other.ts",
  ]);
  expect(environment.readTextFile(libraryPathsOf(fixture).lockFilePath)).not.toMatch(/AGENTS|CLAUDE|GEMINI|cursor/u);
  expect(runCommand(fixture, ["check", "widget"]).exitCode).toBe(SUCCESS_EXIT_CODE);
});

it("refuses to vendor when the locked digests no longer describe the tag", () => {
  const lock = defaultLock(fixture);

  writeLock(fixture, { ...lock, sha256: { ...lock.sha256, "ghost.ts": "cafe", [GREETING_PATH]: "deadbeef" } });

  const result = runCommand(fixture, ["vendor", "widget"]);

  expect(result.exitCode).toBe(FAILURE_EXIT_CODE);
  expect(joined(result)).toContain("packages/widget/upstream.lock.json no longer describes");
  expect(result.messages).toContain("  removed ghost.ts");
  expect(result.messages).toContain("  changed src/greeting.ts");
  expect(fixture.environment.pathExists(libraryPathsOf(fixture).treeDirectory)).toBe(false);
});

it("reports a clone failure and leaves the vendored tree alone", () => {
  writeLock(fixture, { ...defaultLock(fixture), tag: "v99" });

  const result = runCommand(fixture, ["vendor", "widget"]);

  expect(result.exitCode).toBe(FAILURE_EXIT_CODE);
  expect(joined(result)).toContain("at v99 failed:");
  expect(fixture.environment.pathExists(libraryPathsOf(fixture).treeDirectory)).toBe(false);
});

it("reports a sparse-checkout failure", () => {
  writeLock(fixture, { ...defaultLock(fixture), sparsePaths: ["--not-a-path"] });

  const result = runCommand(fixture, ["vendor", "widget"]);

  expect(result.exitCode).toBe(FAILURE_EXIT_CODE);
  expect(joined(result)).toContain("sparse checkout of --not-a-path failed:");
});

it("prints usage when vendor is called without a library and fails on a missing lock", () => {
  const usage = runCommand(fixture, ["vendor"]);

  expect(usage.exitCode).toBe(FAILURE_EXIT_CODE);
  expect(usage.messages).toContain("  node scripts/upstream.ts vendor <lib>");
  expect(runCommand(fixture, ["vendor", "ghost"])).toStrictEqual({
    exitCode: FAILURE_EXIT_CODE,
    messages: ["packages/ghost/upstream.lock.json is missing"],
  });
});

it("discovers packages by glob and passes the check on a freshly vendored tree", () => {
  runCommand(fixture, ["vendor", "widget"]);

  expect(runCommand(fixture, ["check"])).toStrictEqual({
    exitCode: SUCCESS_EXIT_CODE,
    messages: ["packages/widget/upstream matches v1 plus 1 patches"],
  });
});

it("fails the check with the drifted file list when the vendored tree is edited by hand", () => {
  runCommand(fixture, ["vendor", "widget"]);
  writeTree(fixture, GREETING_PATH, `${GREETING_PATCHED}export const extra = true;\n`);
  writeTree(fixture, "src/extra.ts", "export const extra = true;\n");
  fixture.environment.removePath(path.join(libraryPathsOf(fixture).treeDirectory, "src/other.ts"));

  expect(runCommand(fixture, ["check", "widget"])).toStrictEqual({
    exitCode: FAILURE_EXIT_CODE,
    messages: [
      "packages/widget/upstream drifted from v1 plus its patch queue:",
      "  added src/extra.ts",
      "  changed src/greeting.ts",
      "  removed src/other.ts",
    ],
  });
});

it("reports every vendored file as removed when the tree was never vendored", () => {
  expect(runCommand(fixture, ["check", "widget"])).toStrictEqual({
    exitCode: FAILURE_EXIT_CODE,
    messages: [
      "packages/widget/upstream drifted from v1 plus its patch queue:",
      "  removed README.md",
      "  removed src/greeting.ts",
      "  removed src/other.ts",
    ],
  });
});

it("surfaces a preparation failure as a check failure", () => {
  writeLock(fixture, { ...defaultLock(fixture), tag: "v99" });

  const result = runCommand(fixture, ["check", "widget"]);

  expect(result.exitCode).toBe(FAILURE_EXIT_CODE);
  expect(joined(result)).toContain("at v99 failed:");
});

it("fails the check for a named library that declares no lock", () => {
  expect(runCommand(fixture, ["check", "ghost"])).toStrictEqual({
    exitCode: FAILURE_EXIT_CODE,
    messages: ["packages/ghost/upstream.lock.json is missing"],
  });
});

it("succeeds and says so when no package declares an upstream lock", () => {
  fixture.environment.removePath(libraryPathsOf(fixture).lockFilePath);

  expect(runCommand(fixture, ["check"])).toStrictEqual({
    exitCode: SUCCESS_EXIT_CODE,
    messages: ["no packages/*/upstream.lock.json found; nothing to check"],
  });
});

it("succeeds when the repository has no packages directory at all", () => {
  fixture.environment.removePath(path.join(fixture.repositoryRoot, "packages"));

  expect(runCommand(fixture, ["check"])).toStrictEqual({
    exitCode: SUCCESS_EXIT_CODE,
    messages: ["no packages/*/upstream.lock.json found; nothing to check"],
  });
});
