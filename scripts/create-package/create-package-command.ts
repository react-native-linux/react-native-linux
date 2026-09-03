import { buildPackageFiles, parseCreatePackageArguments } from "./create-package-scaffold.ts";
import path from "node:path";

const SUCCESS_EXIT_CODE = 0;
const FAILURE_EXIT_CODE = 1;
const PACKAGES_DIRECTORY_NAME = "packages";

const USAGE_MESSAGES = [
  "usage:",
  "  node scripts/create-package.ts <lib> <upstream-repo-url> <tag> [--sparse <path>...]",
];

interface ScaffoldEnvironment {
  readonly pathExists: (targetPath: string) => boolean;
  readonly writeTextFile: (filePath: string, contents: string) => void;
}

interface CreatePackageResult {
  readonly exitCode: number;
  readonly messages: readonly string[];
}

const libraryPathOf = (libraryName: string): string => `${PACKAGES_DIRECTORY_NAME}/${libraryName}`;

type ScaffoldRequest = NonNullable<ReturnType<typeof parseCreatePackageArguments>>;

const writeScaffold = (
  request: ScaffoldRequest,
  packageDirectory: string,
  environment: ScaffoldEnvironment,
): CreatePackageResult => {
  const files = buildPackageFiles(request);

  for (const file of files) {
    environment.writeTextFile(path.join(packageDirectory, file.relativePath), file.contents);
  }

  return {
    exitCode: SUCCESS_EXIT_CODE,
    messages: [
      `created ${libraryPathOf(request.libraryName)} for ${request.repo} at ${request.tag}`,
      ...files.map((file) => `  ${libraryPathOf(request.libraryName)}/${file.relativePath}`),
      "vendor the locked tag next:",
      `  pnpm upstream:bump ${request.libraryName} ${request.tag}`,
    ],
  };
};

const createPackage = (
  commandArguments: readonly string[],
  repositoryRoot: string,
  environment: ScaffoldEnvironment,
): CreatePackageResult => {
  const request = parseCreatePackageArguments(commandArguments);

  if (request === null) {
    return { exitCode: FAILURE_EXIT_CODE, messages: USAGE_MESSAGES };
  }

  const packageDirectory = path.join(repositoryRoot, PACKAGES_DIRECTORY_NAME, request.libraryName);

  if (environment.pathExists(packageDirectory)) {
    return { exitCode: FAILURE_EXIT_CODE, messages: [`${libraryPathOf(request.libraryName)} already exists`] };
  }

  return writeScaffold(request, packageDirectory, environment);
};

export { createPackage };
