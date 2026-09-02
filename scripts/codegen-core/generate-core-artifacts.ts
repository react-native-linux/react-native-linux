import type { CoreCodegenEnvironment, CoreCodegenPaths } from "./generate-core-artifacts-types.ts";

import type { LibraryConfig } from "@react-native/codegen/lib/generators/RNCodegen.js";
import type { SchemaType } from "@react-native/codegen/lib/CodegenSchema.js";
import { createHash } from "node:crypto";
import path from "node:path";

interface CodegenInput {
  readonly path: string;
  readonly sha256: string;
}

interface CodegenLock {
  readonly codegenVersion: string;
  readonly inputs: readonly CodegenInput[];
  readonly libraryName: string;
  readonly specSourceDirectory: string;
}

const LIBRARY_NAME = "FBReactNativeSpec";
const PACKAGE_NAME = "com.facebook.fbreact.specs";
const LOCK_FILE_NAME = "codegen.lock.json";
const COMPONENT_DIRECTORY_SEGMENTS = ["react", "renderer", "components"] as const;
const OBJECTIVE_CPP_ARTIFACT_NAME = "RCTComponentViewHelpers.h";
const MODULE_ARTIFACTS: LibraryConfig = { generators: ["modulesCxx"] };
const COMPONENT_ARTIFACTS: LibraryConfig = { generators: ["componentsIOS"] };
const JSON_INDENTATION = 2;

const SPEC_FILE_NAME_PATTERN = /^(?:Native[^.]+|[^.]+NativeComponent)\.(?:js|ts|tsx)$/u;
const IGNORED_DIRECTORY_PREFIX = "__";
const IGNORED_SPEC_FILE_NAME = "NativeUIManager.js";
const TURBO_MODULE_PATTERN = /extends TurboModule/u;
const NATIVE_COMPONENT_PATTERN = /export\s+default\s+\(?codegenNativeComponent</u;

const isRecord = (value: unknown): value is Record<string, unknown> =>
  typeof value === "object" && value !== null && !Array.isArray(value);

const toRepositoryRelativePosixPath = (repositoryRoot: string, filePath: string): string =>
  path.relative(repositoryRoot, filePath).split(path.sep).join("/");

const collectCandidatePaths = (
  directoryPath: string,
  environment: Pick<CoreCodegenEnvironment, "listDirectory">,
): string[] => {
  const candidatePaths: string[] = [];

  for (const entry of environment.listDirectory(directoryPath)) {
    const entryPath = path.join(directoryPath, entry.name);

    if (entry.isDirectory) {
      if (!entry.name.startsWith(IGNORED_DIRECTORY_PREFIX)) {
        candidatePaths.push(...collectCandidatePaths(entryPath, environment));
      }
    } else if (SPEC_FILE_NAME_PATTERN.test(entry.name) && entry.name !== IGNORED_SPEC_FILE_NAME) {
      candidatePaths.push(entryPath);
    }
  }

  return candidatePaths;
};

const declaresCodegenSpec = (contents: string): boolean =>
  TURBO_MODULE_PATTERN.test(contents) || NATIVE_COMPONENT_PATTERN.test(contents);

const collectSpecFilePaths = (
  specSourceDirectory: string,
  environment: Pick<CoreCodegenEnvironment, "listDirectory" | "readFile">,
): readonly string[] =>
  collectCandidatePaths(specSourceDirectory, environment)
    .filter((candidatePath) => declaresCodegenSpec(environment.readFile(candidatePath)))
    .toSorted();

const buildCoreSchema = (
  specFilePaths: readonly string[],
  environment: Pick<CoreCodegenEnvironment, "parseSpecFile">,
): SchemaType => {
  const modules: SchemaType["modules"] = {};

  for (const specFilePath of specFilePaths) {
    Object.assign(modules, environment.parseSpecFile(specFilePath).modules);
  }

  return { modules };
};

const readCodegenVersion = (
  codegenPackageJsonPath: string,
  environment: Pick<CoreCodegenEnvironment, "readFile">,
): string => {
  const parsed: unknown = JSON.parse(environment.readFile(codegenPackageJsonPath));

  if (!isRecord(parsed)) {
    throw new Error(`${codegenPackageJsonPath} must contain a JSON object`);
  }

  const { version } = parsed;

  if (typeof version !== "string") {
    throw new TypeError(`${codegenPackageJsonPath} must declare a string "version"`);
  }

  return version;
};

const buildCodegenLock = (
  specFilePaths: readonly string[],
  paths: CoreCodegenPaths,
  environment: Pick<CoreCodegenEnvironment, "readFile">,
): CodegenLock => ({
  codegenVersion: readCodegenVersion(paths.codegenPackageJsonPath, environment),
  inputs: specFilePaths.map((specFilePath) => ({
    path: toRepositoryRelativePosixPath(paths.repositoryRoot, specFilePath),
    sha256: createHash("sha256").update(environment.readFile(specFilePath)).digest("hex"),
  })),
  libraryName: LIBRARY_NAME,
  specSourceDirectory: toRepositoryRelativePosixPath(paths.repositoryRoot, paths.specSourceDirectory),
});

const runCoreCodegen = (paths: CoreCodegenPaths, environment: CoreCodegenEnvironment): void => {
  const specFilePaths = collectSpecFilePaths(paths.specSourceDirectory, environment);
  const schema = buildCoreSchema(specFilePaths, environment);
  const sharedOptions = { assumeNonnull: false, libraryName: LIBRARY_NAME, packageName: PACKAGE_NAME, schema };
  const componentDirectory = path.join(paths.outputDirectory, ...COMPONENT_DIRECTORY_SEGMENTS, LIBRARY_NAME);

  environment.resetDirectory(paths.outputDirectory);
  environment.generateArtifacts(
    { ...sharedOptions, outputDirectory: path.join(paths.outputDirectory, LIBRARY_NAME) },
    MODULE_ARTIFACTS,
  );
  environment.generateArtifacts({ ...sharedOptions, outputDirectory: paths.outputDirectory }, COMPONENT_ARTIFACTS);
  environment.removeFile(path.join(componentDirectory, OBJECTIVE_CPP_ARTIFACT_NAME));
  environment.writeFile(
    path.join(paths.outputDirectory, LOCK_FILE_NAME),
    `${JSON.stringify(buildCodegenLock(specFilePaths, paths, environment), null, JSON_INDENTATION)}\n`,
  );
  environment.report(`Generated ${LIBRARY_NAME} from ${specFilePaths.length} spec files into ${paths.outputDirectory}`);
};

export { buildCodegenLock, collectSpecFilePaths, runCoreCodegen };
