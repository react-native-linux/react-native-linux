import type { CommandResult, LibraryContext, Preparation, UpstreamEnvironment } from "./upstream-types.ts";

import {
  FAILURE_EXIT_CODE,
  LIBRARY_ARGUMENT_INDEX,
  NO_ENTRIES,
  SUCCESS_EXIT_CODE,
  missingLockResult,
  readLibraryContext,
} from "./upstream-library.ts";
import { UPSTREAM_DIRECTORY_NAME, describeLibraryPath, findLibraryNames, prepareTree } from "./upstream-workspace.ts";
import { compareDigests, computeDigests, formatDigestDifferences } from "./upstream-lock.ts";

const completeCheck = (
  context: LibraryContext,
  preparation: Preparation,
  environment: UpstreamEnvironment,
): CommandResult => {
  if (preparation.outcome !== "prepared") {
    environment.removePath(preparation.directory);

    return { exitCode: FAILURE_EXIT_CODE, messages: preparation.failureMessages };
  }

  const differences = compareDigests(
    computeDigests(preparation.directory, environment),
    computeDigests(context.libraryPaths.treeDirectory, environment),
  );
  const vendoredPath = describeLibraryPath(context.libraryName, UPSTREAM_DIRECTORY_NAME);

  environment.removePath(preparation.directory);

  if (differences.length === NO_ENTRIES) {
    return {
      exitCode: SUCCESS_EXIT_CODE,
      messages: [`${vendoredPath} matches ${context.lock.tag} plus ${context.patchFileNames.length} patches`],
    };
  }

  return {
    exitCode: FAILURE_EXIT_CODE,
    messages: [
      `${vendoredPath} drifted from ${context.lock.tag} plus its patch queue:`,
      ...formatDigestDifferences(differences),
    ],
  };
};

const checkLibrary = (libraryName: string, repositoryRoot: string, environment: UpstreamEnvironment): CommandResult => {
  const context = readLibraryContext(libraryName, repositoryRoot, environment);

  if (context === null) {
    return missingLockResult(libraryName);
  }

  return completeCheck(
    context,
    prepareTree({ context, patchCount: context.patchFileNames.length, verifyDigests: true }, environment),
    environment,
  );
};

const checkLibraries = (
  commandArguments: readonly string[],
  repositoryRoot: string,
  environment: UpstreamEnvironment,
): CommandResult => {
  const requestedLibrary = commandArguments[LIBRARY_ARGUMENT_INDEX] ?? null;
  const libraryNames = requestedLibrary === null ? findLibraryNames(repositoryRoot, environment) : [requestedLibrary];

  if (libraryNames.length === NO_ENTRIES) {
    return { exitCode: SUCCESS_EXIT_CODE, messages: ["no packages/*/upstream.lock.json found; nothing to check"] };
  }

  const results = libraryNames.map((libraryName) => checkLibrary(libraryName, repositoryRoot, environment));

  return {
    exitCode: results.some((result) => result.exitCode !== SUCCESS_EXIT_CODE) ? FAILURE_EXIT_CODE : SUCCESS_EXIT_CODE,
    messages: results.flatMap((result) => result.messages),
  };
};

export { checkLibraries };
