import type {
  CommandResult,
  LibraryContext,
  Preparation,
  UpstreamEnvironment,
  UpstreamLock,
} from "./upstream-types.ts";

import {
  FAILURE_EXIT_CODE,
  LIBRARY_ARGUMENT_INDEX,
  SUCCESS_EXIT_CODE,
  VALUE_ARGUMENT_INDEX,
  failedPreparationResult,
  missingLockResult,
  readLibraryContext,
  usageResult,
} from "./upstream-library.ts";
import {
  LOCK_FILE_NAME,
  UPSTREAM_DIRECTORY_NAME,
  describeLibraryPath,
  materializeTree,
  prepareTree,
} from "./upstream-workspace.ts";
import { compareDigests, countDigestDifferences, serializeUpstreamLock } from "./upstream-lock.ts";

const completeVendor = (
  context: LibraryContext,
  preparation: Preparation,
  environment: UpstreamEnvironment,
): CommandResult => {
  if (preparation.outcome !== "prepared") {
    return failedPreparationResult(context, preparation, environment);
  }

  materializeTree(preparation.directory, context.libraryPaths.treeDirectory, environment);
  environment.removePath(preparation.directory);

  return {
    exitCode: SUCCESS_EXIT_CODE,
    messages: [
      `vendored ${describeLibraryPath(context.libraryName, UPSTREAM_DIRECTORY_NAME)} at ${context.lock.tag}`,
      `  upstream files: ${Object.keys(preparation.pristineDigests).length}`,
      `  patches applied: ${context.patchFileNames.length}`,
    ],
  };
};

const vendorLibrary = (
  commandArguments: readonly string[],
  repositoryRoot: string,
  environment: UpstreamEnvironment,
): CommandResult => {
  const libraryName = commandArguments[LIBRARY_ARGUMENT_INDEX] ?? null;

  if (libraryName === null) {
    return usageResult();
  }

  const context = readLibraryContext(libraryName, repositoryRoot, environment);

  if (context === null) {
    return missingLockResult(libraryName);
  }

  return completeVendor(
    context,
    prepareTree({ context, patchCount: context.patchFileNames.length, verifyDigests: true }, environment),
    environment,
  );
};

const summarizeBump = (
  context: LibraryContext,
  previousLock: UpstreamLock,
  pristineDigests: Readonly<Record<string, string>>,
): readonly string[] => {
  const differences = compareDigests(previousLock.sha256, pristineDigests);
  const counts = `${countDigestDifferences(differences, "added")} added, ${countDigestDifferences(differences, "removed")} removed, ${countDigestDifferences(differences, "changed")} changed`;

  return [
    `bumped ${context.libraryName} from ${previousLock.tag} to ${context.lock.tag}`,
    `  upstream files: ${counts}`,
    `  patches applied: ${context.patchFileNames.length}`,
    `  rewrote ${describeLibraryPath(context.libraryName, LOCK_FILE_NAME)}`,
  ];
};

const completeBump = (
  context: LibraryContext,
  previousLock: UpstreamLock,
  environment: UpstreamEnvironment,
): CommandResult => {
  const preparation = prepareTree(
    { context, patchCount: context.patchFileNames.length, verifyDigests: false },
    environment,
  );

  if (preparation.outcome === "cloneFailed") {
    environment.removePath(preparation.directory);

    return { exitCode: FAILURE_EXIT_CODE, messages: preparation.failureMessages };
  }

  environment.writeTextFile(
    context.libraryPaths.lockFilePath,
    serializeUpstreamLock({ ...context.lock, sha256: preparation.pristineDigests }),
  );

  if (preparation.outcome !== "prepared") {
    return failedPreparationResult(context, preparation, environment);
  }

  materializeTree(preparation.directory, context.libraryPaths.treeDirectory, environment);
  environment.removePath(preparation.directory);

  return { exitCode: SUCCESS_EXIT_CODE, messages: summarizeBump(context, previousLock, preparation.pristineDigests) };
};

const bumpLibrary = (
  commandArguments: readonly string[],
  repositoryRoot: string,
  environment: UpstreamEnvironment,
): CommandResult => {
  const libraryName = commandArguments[LIBRARY_ARGUMENT_INDEX] ?? null;
  const tag = commandArguments[VALUE_ARGUMENT_INDEX] ?? null;

  if (libraryName === null || tag === null) {
    return usageResult();
  }

  const context = readLibraryContext(libraryName, repositoryRoot, environment);

  if (context === null) {
    return missingLockResult(libraryName);
  }

  return completeBump({ ...context, lock: { ...context.lock, tag } }, context.lock, environment);
};

export { bumpLibrary, vendorLibrary };
