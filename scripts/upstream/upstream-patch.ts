import type { CaptureRequest, CommandResult, LibraryContext, UpstreamEnvironment } from "./upstream-types.ts";

import {
  FAILURE_EXIT_CODE,
  LIBRARY_ARGUMENT_INDEX,
  NO_ENTRIES,
  SUCCESS_EXIT_CODE,
  VALUE_ARGUMENT_INDEX,
  failedPreparationResult,
  missingLockResult,
  readLibraryContext,
  usageResult,
} from "./upstream-library.ts";
import {
  UPSTREAM_DIRECTORY_NAME,
  capturePatchDiff,
  describeLibraryPath,
  patchFileNameOf,
  patchNameOf,
  prepareTree,
} from "./upstream-workspace.ts";
import path from "node:path";

const NOT_FOUND_INDEX = -1;
const PATCH_NUMBER_OFFSET = 1;
const PATCH_NAME_PATTERN = /^[a-z0-9][a-z0-9-]*$/u;
const PATCHES_RELATIVE_PREFIX = "patches";

const validateCaptureRequest = (
  context: LibraryContext,
  patchName: string,
  environment: Pick<UpstreamEnvironment, "pathExists">,
): CommandResult | null => {
  if (!PATCH_NAME_PATTERN.test(patchName)) {
    return {
      exitCode: FAILURE_EXIT_CODE,
      messages: [`patch name "${patchName}" must match ${PATCH_NAME_PATTERN.source}`],
    };
  }

  if (!environment.pathExists(context.libraryPaths.treeDirectory)) {
    return {
      exitCode: FAILURE_EXIT_CODE,
      messages: [
        `${describeLibraryPath(context.libraryName, UPSTREAM_DIRECTORY_NAME)} is missing; run pnpm upstream:vendor ${context.libraryName} first`,
      ],
    };
  }

  return null;
};

const writeCapturedPatch = (request: CaptureRequest, environment: UpstreamEnvironment): CommandResult => {
  const { context } = request;
  const diff = capturePatchDiff(request.preparedDirectory, context.libraryPaths.treeDirectory, environment);

  environment.removePath(request.preparedDirectory);

  if (diff.length === NO_ENTRIES) {
    return {
      exitCode: FAILURE_EXIT_CODE,
      messages: [
        `${describeLibraryPath(context.libraryName, UPSTREAM_DIRECTORY_NAME)} matches the patch queue; nothing to capture`,
      ],
    };
  }

  environment.writeTextFile(path.join(context.libraryPaths.patchesDirectory, request.patchFileName), diff);

  return {
    exitCode: SUCCESS_EXIT_CODE,
    messages: [
      `captured ${describeLibraryPath(context.libraryName, `${PATCHES_RELATIVE_PREFIX}/${request.patchFileName}`)}`,
    ],
  };
};

const completeCapture = (
  context: LibraryContext,
  patchName: string,
  environment: UpstreamEnvironment,
): CommandResult => {
  const invalidRequest = validateCaptureRequest(context, patchName, environment);

  if (invalidRequest !== null) {
    return invalidRequest;
  }

  const existingIndex = context.patchFileNames.findIndex((fileName) => patchNameOf(fileName) === patchName);
  const patchCount = existingIndex === NOT_FOUND_INDEX ? context.patchFileNames.length : existingIndex;
  const patchFileName =
    context.patchFileNames[existingIndex] ?? patchFileNameOf(patchCount + PATCH_NUMBER_OFFSET, patchName);
  const preparation = prepareTree({ context, patchCount, verifyDigests: true }, environment);

  if (preparation.outcome !== "prepared") {
    return failedPreparationResult(context, preparation, environment);
  }

  return writeCapturedPatch({ context, patchFileName, preparedDirectory: preparation.directory }, environment);
};

const capturePatch = (
  commandArguments: readonly string[],
  repositoryRoot: string,
  environment: UpstreamEnvironment,
): CommandResult => {
  const libraryName = commandArguments[LIBRARY_ARGUMENT_INDEX] ?? null;
  const patchName = commandArguments[VALUE_ARGUMENT_INDEX] ?? null;

  if (libraryName === null || patchName === null) {
    return usageResult();
  }

  const context = readLibraryContext(libraryName, repositoryRoot, environment);

  if (context === null) {
    return missingLockResult(libraryName);
  }

  return completeCapture(context, patchName, environment);
};

export { capturePatch };
