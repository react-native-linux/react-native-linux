import type { CommandResult, LibraryPaths, UpstreamEnvironment, UpstreamLock } from "./upstream-types.ts";

import { computeDigests, serializeUpstreamLock } from "./upstream-lock.ts";
import { createUpstreamEnvironment } from "./upstream-environment.ts";
import path from "node:path";
import { resolveLibraryPaths } from "./upstream-workspace.ts";
import { runUpstreamCommand } from "./upstream-commands.ts";

const LIBRARY_NAME = "widget";
const PATCH_FILE_NAME = "0001-linux-greeting.patch";
const PATCH_NAME = "linux-greeting";
const GREETING_PATH = "src/greeting.ts";
const GREETING_V1 = 'export const greeting = "hello";\n';
const GREETING_PATCHED = 'export const greeting = "hello linux";\n';
const GREETING_V3 = 'export const greeting = "bonjour";\n';

interface OriginVersion {
  readonly files: Readonly<Record<string, string>>;
  readonly removals: readonly string[];
  readonly tag: string;
}

const ORIGIN_VERSIONS: readonly OriginVersion[] = [
  {
    files: { "README.md": "widget\n", "src/greeting.ts": GREETING_V1, "src/other.ts": "export const other = true;\n" },
    removals: [],
    tag: "v1",
  },
  {
    files: { "README.md": "widget two\n", "src/added.ts": "export const added = true;\n" },
    removals: ["src/other.ts"],
    tag: "v2",
  },
  { files: { "src/greeting.ts": GREETING_V3 }, removals: [], tag: "v3" },
];

interface UpstreamFixture {
  readonly environment: UpstreamEnvironment;
  readonly originDirectory: string;
  readonly patchContents: string;
  readonly pristineDigests: Readonly<Record<string, string>>;
  readonly repositoryRoot: string;
  readonly temporaryRoot: string;
}

const initializeRepository = (
  environment: UpstreamEnvironment,
  originDirectory: string,
  temporaryRoot: string,
): void => {
  environment.runGit(["init", "--quiet", "--initial-branch=main", originDirectory], temporaryRoot);
  environment.runGit(["config", "user.email", "fixture@example.invalid"], originDirectory);
  environment.runGit(["config", "user.name", "fixture"], originDirectory);
  environment.runGit(["config", "commit.gpgsign", "false"], originDirectory);
};

const commitVersion = (environment: UpstreamEnvironment, originDirectory: string, version: OriginVersion): void => {
  for (const [relativePath, contents] of Object.entries(version.files)) {
    environment.writeTextFile(path.join(originDirectory, relativePath), contents);
  }

  for (const relativePath of version.removals) {
    environment.removePath(path.join(originDirectory, relativePath));
  }

  environment.runGit(["add", "--all"], originDirectory);
  environment.runGit(["commit", "--quiet", "--message", version.tag], originDirectory);
  environment.runGit(["tag", version.tag], originDirectory);
};

const buildOriginRepository = (
  environment: UpstreamEnvironment,
  originDirectory: string,
  temporaryRoot: string,
): void => {
  initializeRepository(environment, originDirectory, temporaryRoot);

  for (const version of ORIGIN_VERSIONS) {
    commitVersion(environment, originDirectory, version);
  }
};

const captureFixturePatch = (
  environment: UpstreamEnvironment,
  originDirectory: string,
): Pick<UpstreamFixture, "patchContents" | "pristineDigests"> => {
  const scratchDirectory = environment.createTemporaryDirectory();

  environment.runGit(["clone", "--quiet", "--branch", "v1", `file://${originDirectory}`, "."], scratchDirectory);

  const pristineDigests = computeDigests(scratchDirectory, environment);

  environment.writeTextFile(path.join(scratchDirectory, GREETING_PATH), GREETING_PATCHED);

  const patchContents = environment.runGit(
    ["diff", "--src-prefix=a/", "--dst-prefix=b/"],
    scratchDirectory,
  ).standardOutput;

  environment.removePath(scratchDirectory);

  return { patchContents, pristineDigests };
};

const createUpstreamFixture = (): UpstreamFixture => {
  const environment = createUpstreamEnvironment();
  const temporaryRoot = environment.createTemporaryDirectory();
  const originDirectory = path.join(temporaryRoot, "origin");

  buildOriginRepository(environment, originDirectory, temporaryRoot);

  return {
    ...captureFixturePatch(environment, originDirectory),
    environment,
    originDirectory,
    repositoryRoot: path.join(temporaryRoot, "workspace"),
    temporaryRoot,
  };
};

const libraryPathsOf = (fixture: UpstreamFixture): LibraryPaths =>
  resolveLibraryPaths(fixture.repositoryRoot, LIBRARY_NAME);

const defaultLock = (fixture: UpstreamFixture): UpstreamLock => ({
  repo: `file://${fixture.originDirectory}`,
  sha256: fixture.pristineDigests,
  sparsePaths: ["src"],
  tag: "v1",
});

const writeLock = (fixture: UpstreamFixture, lock: UpstreamLock): void => {
  fixture.environment.writeTextFile(libraryPathsOf(fixture).lockFilePath, serializeUpstreamLock(lock));
};

const resetWorkspace = (fixture: UpstreamFixture): void => {
  const { environment } = fixture;
  const packagesDirectory = path.join(fixture.repositoryRoot, "packages");

  environment.removePath(packagesDirectory);
  writeLock(fixture, defaultLock(fixture));
  environment.writeTextFile(
    path.join(libraryPathsOf(fixture).patchesDirectory, PATCH_FILE_NAME),
    fixture.patchContents,
  );
  environment.writeTextFile(path.join(libraryPathsOf(fixture).patchesDirectory, "README.md"), "queue notes\n");
  environment.writeTextFile(path.join(packagesDirectory, "notes", "package.json"), "{}\n");
  environment.writeTextFile(path.join(packagesDirectory, "readme.txt"), "packages\n");
};

const removeFixture = (fixture: UpstreamFixture): void => {
  fixture.environment.removePath(fixture.temporaryRoot);
};

const runCommand = (fixture: UpstreamFixture, commandArguments: readonly string[]): CommandResult =>
  runUpstreamCommand(commandArguments, fixture.repositoryRoot, fixture.environment);

const readTree = (fixture: UpstreamFixture, relativePath: string): string =>
  fixture.environment.readTextFile(path.join(libraryPathsOf(fixture).treeDirectory, relativePath));

const writeTree = (fixture: UpstreamFixture, relativePath: string, contents: string): void => {
  fixture.environment.writeTextFile(path.join(libraryPathsOf(fixture).treeDirectory, relativePath), contents);
};

export {
  GREETING_PATCHED,
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
};
