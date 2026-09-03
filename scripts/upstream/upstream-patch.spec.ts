import {
  GREETING_PATH,
  GREETING_V3,
  LIBRARY_NAME,
  PATCH_FILE_NAME,
  PATCH_NAME,
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

import type { CommandResult } from "./upstream-types.ts";
import path from "node:path";

const SUCCESS_EXIT_CODE = 0;
const FAILURE_EXIT_CODE = 1;
const RESOLVED_GREETING = 'export const greeting = "bonjour linux";\n';

const fixture = createUpstreamFixture();

const joined = (result: CommandResult): string => result.messages.join("\n");

const readLockFile = (): string => fixture.environment.readTextFile(libraryPathsOf(fixture).lockFilePath);

const readPatchFile = (patchFileName: string): string =>
  fixture.environment.readTextFile(path.join(libraryPathsOf(fixture).patchesDirectory, patchFileName));

afterAll(() => {
  removeFixture(fixture);
});

beforeEach(() => {
  resetWorkspace(fixture);
});

it("bumps to a tag where the queue still applies, rewrites the lock and summarizes the delta", () => {
  const result = runCommand(fixture, ["bump", LIBRARY_NAME, "v2"]);

  expect(result).toStrictEqual({
    exitCode: SUCCESS_EXIT_CODE,
    messages: [
      "bumped widget from v1 to v2",
      "  upstream files: 1 added, 1 removed, 1 changed",
      "  patches applied: 1",
      "  rewrote packages/widget/upstream.lock.json",
    ],
  });
  expect(readLockFile()).toContain('"tag": "v2"');
  expect(readLockFile()).toContain('"src/added.ts"');
  expect(readTree(fixture, GREETING_PATH)).toContain("hello linux");
  expect(readTree(fixture, "src/added.ts")).toBe("export const added = true;\n");
});

it("leaves the lock untouched when the new tag cannot be cloned", () => {
  const result = runCommand(fixture, ["bump", LIBRARY_NAME, "v99"]);

  expect(result.exitCode).toBe(FAILURE_EXIT_CODE);
  expect(joined(result)).toContain("at v99 failed:");
  expect(readLockFile()).toContain('"tag": "v1"');
});

it("leaves a bumped tree that passes the drift check", () => {
  runCommand(fixture, ["bump", LIBRARY_NAME, "v2"]);

  expect(runCommand(fixture, ["check"]).exitCode).toBe(SUCCESS_EXIT_CODE);
});

it("stops on the patch that does not apply and leaves a tree the user can fix", () => {
  const result = runCommand(fixture, ["bump", LIBRARY_NAME, "v3"]);

  expect(result.exitCode).toBe(FAILURE_EXIT_CODE);
  expect(joined(result)).toContain("0001-linux-greeting.patch does not apply to v3:");
  expect(result.messages).toContain("Resolve the conflict there, then run:");
  expect(result.messages).toContain("  pnpm upstream:patch widget linux-greeting");
  expect(result.messages).toContain("  pnpm upstream:bump widget v3");
  expect(readLockFile()).toContain('"tag": "v3"');
  expect(readTree(fixture, GREETING_PATH)).toContain(GREETING_V3.trim());
});

it("finishes the bump after the conflicting patch is resolved and refreshed in place", () => {
  runCommand(fixture, ["bump", LIBRARY_NAME, "v3"]);
  writeTree(fixture, GREETING_PATH, RESOLVED_GREETING);

  const refreshed = runCommand(fixture, ["patch", LIBRARY_NAME, PATCH_NAME]);

  expect(refreshed).toStrictEqual({
    exitCode: SUCCESS_EXIT_CODE,
    messages: ["captured packages/widget/patches/0001-linux-greeting.patch"],
  });
  expect(readPatchFile(PATCH_FILE_NAME)).toContain('+export const greeting = "bonjour linux";');
  expect(runCommand(fixture, ["bump", LIBRARY_NAME, "v3"]).exitCode).toBe(SUCCESS_EXIT_CODE);
  expect(runCommand(fixture, ["check"]).exitCode).toBe(SUCCESS_EXIT_CODE);
});

it("prints usage when bump is called without a tag and fails on a missing lock", () => {
  const usage = runCommand(fixture, ["bump", LIBRARY_NAME]);

  expect(usage.messages).toContain("  node scripts/upstream.ts bump <lib> <tag>");
  expect(runCommand(fixture, ["bump"]).exitCode).toBe(FAILURE_EXIT_CODE);
  expect(runCommand(fixture, ["bump", "ghost", "v1"])).toStrictEqual({
    exitCode: FAILURE_EXIT_CODE,
    messages: ["packages/ghost/upstream.lock.json is missing"],
  });
});

it("captures the working diff of the vendored tree as the next numbered patch", () => {
  runCommand(fixture, ["vendor", LIBRARY_NAME]);
  writeTree(fixture, "src/linux.ts", "export const linux = true;\n");
  writeTree(fixture, "README.md", "widget on linux\n");

  const result = runCommand(fixture, ["patch", LIBRARY_NAME, "linux-entry"]);

  expect(result).toStrictEqual({
    exitCode: SUCCESS_EXIT_CODE,
    messages: ["captured packages/widget/patches/0002-linux-entry.patch"],
  });
  expect(readPatchFile("0002-linux-entry.patch")).toContain("+++ b/src/linux.ts");
  expect(runCommand(fixture, ["check"]).exitCode).toBe(SUCCESS_EXIT_CODE);
});

it("refuses to capture a patch when the tree already matches the queue", () => {
  runCommand(fixture, ["vendor", LIBRARY_NAME]);

  expect(runCommand(fixture, ["patch", LIBRARY_NAME, "linux-entry"])).toStrictEqual({
    exitCode: FAILURE_EXIT_CODE,
    messages: ["packages/widget/upstream matches the patch queue; nothing to capture"],
  });
});

it("rejects a patch name that is not lowercase kebab case", () => {
  const result = runCommand(fixture, ["patch", LIBRARY_NAME, "Linux Entry"]);

  expect(result.exitCode).toBe(FAILURE_EXIT_CODE);
  expect(joined(result)).toContain('patch name "Linux Entry" must match');
});

it("requires a vendored tree before capturing a patch", () => {
  expect(runCommand(fixture, ["patch", LIBRARY_NAME, "linux-entry"])).toStrictEqual({
    exitCode: FAILURE_EXIT_CODE,
    messages: ["packages/widget/upstream is missing; run pnpm upstream:vendor widget first"],
  });
});

it("reports a preparation failure instead of capturing a patch", () => {
  runCommand(fixture, ["vendor", LIBRARY_NAME]);
  writeLock(fixture, { ...defaultLock(fixture), tag: "v99" });

  const result = runCommand(fixture, ["patch", LIBRARY_NAME, "linux-entry"]);

  expect(result.exitCode).toBe(FAILURE_EXIT_CODE);
  expect(joined(result)).toContain("at v99 failed:");
});

it("prints usage when patch is called without a name and fails on a missing lock", () => {
  const usage = runCommand(fixture, ["patch", LIBRARY_NAME]);

  expect(usage.messages).toContain("  node scripts/upstream.ts patch <lib> <name>");
  expect(runCommand(fixture, ["patch"]).exitCode).toBe(FAILURE_EXIT_CODE);
  expect(runCommand(fixture, ["patch", "ghost", "linux-entry"])).toStrictEqual({
    exitCode: FAILURE_EXIT_CODE,
    messages: ["packages/ghost/upstream.lock.json is missing"],
  });
});

it("prints usage for an unknown command and for no command at all", () => {
  expect(runCommand(fixture, ["release"]).exitCode).toBe(FAILURE_EXIT_CODE);
  expect(runCommand(fixture, []).messages).toContain("usage:");
});
