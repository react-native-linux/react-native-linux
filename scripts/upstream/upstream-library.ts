import type { CommandResult, LibraryContext, Preparation, UpstreamEnvironment } from "./upstream-types.ts";

import {
  LOCK_FILE_NAME,
  describeLibraryPath,
  listPatchFileNames,
  materializeTree,
  resolveLibraryPaths,
} from "./upstream-workspace.ts";
import { parseUpstreamLock } from "./upstream-lock.ts";

const SUCCESS_EXIT_CODE = 0;
const FAILURE_EXIT_CODE = 1;
const LIBRARY_ARGUMENT_INDEX = 1;
const VALUE_ARGUMENT_INDEX = 2;
const NO_ENTRIES = 0;

const USAGE_MESSAGES = [
  "usage:",
  "  node scripts/upstream.ts vendor <lib>",
  "  node scripts/upstream.ts bump <lib> <tag>",
  "  node scripts/upstream.ts patch <lib> <name>",
  "  node scripts/upstream.ts check [<lib>]",
];

const usageResult = (): CommandResult => ({ exitCode: FAILURE_EXIT_CODE, messages: USAGE_MESSAGES });

const missingLockResult = (libraryName: string): CommandResult => ({
  exitCode: FAILURE_EXIT_CODE,
  messages: [`${describeLibraryPath(libraryName, LOCK_FILE_NAME)} is missing`],
});

const readLibraryContext = (
  libraryName: string,
  repositoryRoot: string,
  environment: UpstreamEnvironment,
): LibraryContext | null => {
  const libraryPaths = resolveLibraryPaths(repositoryRoot, libraryName);

  if (!environment.pathExists(libraryPaths.lockFilePath)) {
    return null;
  }

  return {
    libraryName,
    libraryPaths,
    lock: parseUpstreamLock(environment.readTextFile(libraryPaths.lockFilePath), libraryPaths.lockFilePath),
    patchFileNames: listPatchFileNames(libraryPaths, environment),
  };
};

const failedPreparationResult = (
  context: LibraryContext,
  preparation: Preparation,
  environment: UpstreamEnvironment,
): CommandResult => {
  if (preparation.outcome === "patchFailed") {
    materializeTree(preparation.directory, context.libraryPaths.treeDirectory, environment);
  }

  environment.removePath(preparation.directory);

  return { exitCode: FAILURE_EXIT_CODE, messages: preparation.failureMessages };
};

export {
  FAILURE_EXIT_CODE,
  LIBRARY_ARGUMENT_INDEX,
  NO_ENTRIES,
  SUCCESS_EXIT_CODE,
  VALUE_ARGUMENT_INDEX,
  failedPreparationResult,
  missingLockResult,
  readLibraryContext,
  usageResult,
};
