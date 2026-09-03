import type {
  LibraryContext,
  LibraryPaths,
  Preparation,
  PreparationRequest,
  UpstreamEnvironment,
  UpstreamLock,
} from "./upstream-types.ts";

import { compareDigests, computeDigests, formatDigestDifferences } from "./upstream-lock.ts";
import path from "node:path";

const LOCK_FILE_NAME = "upstream.lock.json";
const PACKAGES_DIRECTORY_NAME = "packages";
const PATCHES_DIRECTORY_NAME = "patches";
const UPSTREAM_DIRECTORY_NAME = "upstream";
const PATCH_FILE_SUFFIX = ".patch";
const PATCH_NUMBER_PREFIX_LENGTH = 5;
const PATCH_NUMBER_WIDTH = 4;
const SUCCESS_EXIT_CODE = 0;
const NO_ENTRIES = 0;
const FIRST_INDEX = 0;

const CLONE_ARGUMENTS = ["clone", "--filter=blob:none", "--sparse", "--depth", "1", "--branch"] as const;
const COMMIT_ARGUMENTS = [
  "-c",
  "commit.gpgsign=false",
  "-c",
  "user.email=upstream@react-native-linux.invalid",
  "-c",
  "user.name=react-native-linux",
  "commit",
  "--quiet",
  "--allow-empty",
  "--message",
  "vendored upstream plus the patch queue",
] as const;
const DIFF_ARGUMENTS = ["diff", "--cached", "--binary", "--src-prefix=a/", "--dst-prefix=b/", "HEAD"] as const;

const describeLibraryPath = (libraryName: string, relativePath: string): string =>
  `${PACKAGES_DIRECTORY_NAME}/${libraryName}/${relativePath}`;

const resolveLibraryPaths = (repositoryRoot: string, libraryName: string): LibraryPaths => {
  const libraryDirectory = path.join(repositoryRoot, PACKAGES_DIRECTORY_NAME, libraryName);

  return {
    lockFilePath: path.join(libraryDirectory, LOCK_FILE_NAME),
    patchesDirectory: path.join(libraryDirectory, PATCHES_DIRECTORY_NAME),
    treeDirectory: path.join(libraryDirectory, UPSTREAM_DIRECTORY_NAME),
  };
};

const findLibraryNames = (
  repositoryRoot: string,
  environment: Pick<UpstreamEnvironment, "listDirectories" | "pathExists">,
): readonly string[] =>
  environment
    .listDirectories(path.join(repositoryRoot, PACKAGES_DIRECTORY_NAME))
    .filter((libraryName) => environment.pathExists(resolveLibraryPaths(repositoryRoot, libraryName).lockFilePath))
    .toSorted();

const listPatchFileNames = (
  libraryPaths: LibraryPaths,
  environment: Pick<UpstreamEnvironment, "listFiles">,
): readonly string[] =>
  environment
    .listFiles(libraryPaths.patchesDirectory)
    .filter((fileName) => fileName.endsWith(PATCH_FILE_SUFFIX))
    .toSorted();

const patchNameOf = (patchFileName: string): string =>
  patchFileName.slice(PATCH_NUMBER_PREFIX_LENGTH, -PATCH_FILE_SUFFIX.length);

const patchFileNameOf = (patchNumber: number, patchName: string): string =>
  `${String(patchNumber).padStart(PATCH_NUMBER_WIDTH, "0")}-${patchName}${PATCH_FILE_SUFFIX}`;

const describeConflictPlaybook = (context: LibraryContext, patchFileName: string): readonly string[] => [
  `${describeLibraryPath(context.libraryName, UPSTREAM_DIRECTORY_NAME)} holds ${context.lock.tag} with the queue applied up to ${patchFileName}.`,
  "Resolve the conflict there, then run:",
  `  pnpm upstream:patch ${context.libraryName} ${patchNameOf(patchFileName)}`,
  `  pnpm upstream:bump ${context.libraryName} ${context.lock.tag}`,
];

const cloneUpstream = (lock: UpstreamLock, directory: string, environment: UpstreamEnvironment): readonly string[] => {
  const cloned = environment.runGit([...CLONE_ARGUMENTS, lock.tag, lock.repo, "."], directory);

  if (cloned.exitCode !== SUCCESS_EXIT_CODE) {
    return [`cloning ${lock.repo} at ${lock.tag} failed:`, cloned.standardError];
  }

  const sparse = environment.runGit(["sparse-checkout", "set", "--cone", ...lock.sparsePaths], directory);

  if (sparse.exitCode !== SUCCESS_EXIT_CODE) {
    return [`sparse checkout of ${lock.sparsePaths.join(", ")} failed:`, sparse.standardError];
  }

  return [];
};

const describeLockMismatch = (
  request: PreparationRequest,
  pristineDigests: Readonly<Record<string, string>>,
): readonly string[] => {
  const differences = request.verifyDigests ? compareDigests(request.context.lock.sha256, pristineDigests) : [];

  if (differences.length === NO_ENTRIES) {
    return [];
  }

  return [
    `${describeLibraryPath(request.context.libraryName, LOCK_FILE_NAME)} no longer describes ${request.context.lock.repo} at ${request.context.lock.tag}:`,
    ...formatDigestDifferences(differences),
  ];
};

const applyPatchQueue = (
  request: PreparationRequest,
  directory: string,
  environment: UpstreamEnvironment,
): Pick<Preparation, "failureMessages" | "outcome"> => {
  for (const patchFileName of request.context.patchFileNames.slice(FIRST_INDEX, request.patchCount)) {
    const applied = environment.applyPatch(
      path.join(request.context.libraryPaths.patchesDirectory, patchFileName),
      directory,
    );

    if (applied.exitCode !== SUCCESS_EXIT_CODE) {
      return {
        failureMessages: [
          `${patchFileName} does not apply to ${request.context.lock.tag}:`,
          applied.standardError,
          ...describeConflictPlaybook(request.context, patchFileName),
        ],
        outcome: "patchFailed",
      };
    }
  }

  return { failureMessages: [], outcome: "prepared" };
};

const prepareTree = (request: PreparationRequest, environment: UpstreamEnvironment): Preparation => {
  const directory = environment.createTemporaryDirectory();
  const cloneFailure = cloneUpstream(request.context.lock, directory, environment);

  if (cloneFailure.length !== NO_ENTRIES) {
    return { directory, failureMessages: cloneFailure, outcome: "cloneFailed", pristineDigests: {} };
  }

  const pristineDigests = computeDigests(directory, environment);
  const lockMismatch = describeLockMismatch(request, pristineDigests);

  if (lockMismatch.length !== NO_ENTRIES) {
    return { directory, failureMessages: lockMismatch, outcome: "digestMismatch", pristineDigests };
  }

  return { ...applyPatchQueue(request, directory, environment), directory, pristineDigests };
};

const materializeTree = (
  preparedDirectory: string,
  treeDirectory: string,
  environment: Pick<UpstreamEnvironment, "copyDirectory" | "removePath">,
): void => {
  environment.removePath(treeDirectory);
  environment.copyDirectory(preparedDirectory, treeDirectory);
};

const capturePatchDiff = (
  preparedDirectory: string,
  treeDirectory: string,
  environment: UpstreamEnvironment,
): string => {
  environment.runGit(["add", "--all"], preparedDirectory);
  environment.runGit([...COMMIT_ARGUMENTS], preparedDirectory);

  for (const relativePath of environment.listFiles(preparedDirectory)) {
    environment.removePath(path.join(preparedDirectory, relativePath));
  }

  environment.copyDirectory(treeDirectory, preparedDirectory);
  environment.runGit(["add", "--all"], preparedDirectory);

  return environment.runGit([...DIFF_ARGUMENTS], preparedDirectory).standardOutput;
};

export {
  LOCK_FILE_NAME,
  UPSTREAM_DIRECTORY_NAME,
  capturePatchDiff,
  describeLibraryPath,
  findLibraryNames,
  listPatchFileNames,
  materializeTree,
  patchFileNameOf,
  patchNameOf,
  prepareTree,
  resolveLibraryPaths,
};
