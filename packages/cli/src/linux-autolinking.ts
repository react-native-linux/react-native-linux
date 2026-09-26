import type {
  AutolinkingDependency,
  AutolinkingRequest,
  AutolinkingVerdict,
  NativeBuildDescriptor,
} from "./linux-autolinking-types.ts";
import path from "node:path";

interface NonPortableInclude {
  readonly include: string;
  readonly pattern: RegExp;
}

const linuxPlatformName = "linux";
const nitroConfigFileName = "nitro.json";
const expoModuleConfigFileName = "expo-module.config.json";
const defaultCMakeListsFileName = "CMakeLists.txt";
const portableImplementationKey = "all";
const currentDirectoryVariablePattern = /\$\{CMAKE_CURRENT_(?:LIST|SOURCE)_DIR\}\//gu;
const sourceReferencePattern = /[^\s"()]+\.(?:c|cc|cpp|cxx|h|hpp|m|mm)(?=[\s")]|$)/gu;

const nonPortableIncludes: readonly NonPortableInclude[] = [
  { include: "<jni.h>", pattern: /^\s*#\s*include\s*<jni\.h>/mu },
  { include: "<fbjni/", pattern: /^\s*#\s*include\s*<fbjni\//mu },
  { include: "#import", pattern: /^\s*#\s*import\b/mu },
  { include: "an Apple framework header", pattern: /^\s*#\s*include\s*<(?:AppKit|Foundation|UIKit)\//mu },
];

const isRecord = (value: unknown): value is Record<string, unknown> =>
  typeof value === "object" && value !== null && !Array.isArray(value);

const readJsonObject = (request: AutolinkingRequest, filePath: string): Record<string, unknown> | null => {
  const contents = request.readFile(filePath);

  if (contents === null) {
    return null;
  }

  const parsed: unknown = JSON.parse(contents);

  if (!isRecord(parsed)) {
    throw new TypeError(`${filePath} must contain a JSON object`);
  }

  return parsed;
};

interface LinkTarget {
  readonly cmakeListsPath: string;
  readonly descriptor: NativeBuildDescriptor;
  readonly packageName: string;
}

const linked = (
  rule: "cxx-fallback" | "explicit",
  { cmakeListsPath, descriptor, packageName }: LinkTarget,
): AutolinkingVerdict => ({
  cmakeListsPath,
  kind: "linked",
  message: `${packageName}: linked by the ${rule} rule from ${cmakeListsPath}`,
  moduleHeaderName: descriptor.cxxModuleHeaderName ?? null,
  moduleName: descriptor.cxxModuleCMakeListsModuleName ?? null,
  packageName,
  rule,
});

const linkExplicitly = (dependency: AutolinkingDependency, linux: NativeBuildDescriptor): AutolinkingVerdict => {
  const sourceDirectory = linux.sourceDir ?? path.join(dependency.root, linuxPlatformName);
  const cmakeListsPath =
    linux.cxxModuleCMakeListsPath ?? linux.cmakeListsPath ?? path.join(sourceDirectory, defaultCMakeListsFileName);

  return linked("explicit", { cmakeListsPath, descriptor: linux, packageName: dependency.name });
};

const describeUnimplemented = (unimplemented: readonly string[], configPath: string): string => {
  const names = unimplemented.join(", ");

  return names === "" ? "" : `; no "all" implementation in ${configPath} for ${names}, which throw when created`;
};

const classifyNitro = (
  packageName: string,
  configPath: string,
  config: Record<string, unknown>,
): AutolinkingVerdict => {
  const { autolinking: declared } = config;
  const autolinking = isRecord(declared) ? declared : {};
  const hybridObjectNames = Object.keys(autolinking).toSorted();
  const isPortable = (name: string): boolean => {
    const entry = autolinking[name];

    return isRecord(entry) && portableImplementationKey in entry;
  };
  const compiled = hybridObjectNames.filter((name) => isPortable(name));
  const unimplemented = hybridObjectNames.filter((name) => !isPortable(name));

  return {
    compiled,
    kind: "nitro",
    message: `${packageName}: Nitro hybrid objects compiled: ${compiled.join(", ") || "none"}${describeUnimplemented(unimplemented, configPath)}`,
    packageName,
    unimplemented,
  };
};

const lacksLinuxPlatform = ({ platforms }: Record<string, unknown>): boolean =>
  !Array.isArray(platforms) || !platforms.includes(linuxPlatformName);

const classifyByConfigFile = (
  request: AutolinkingRequest,
  dependency: AutolinkingDependency,
): AutolinkingVerdict | null => {
  const nitroConfigPath = path.join(dependency.root, nitroConfigFileName);
  const nitroConfig = readJsonObject(request, nitroConfigPath);

  if (nitroConfig !== null) {
    return classifyNitro(dependency.name, nitroConfigPath, nitroConfig);
  }

  const expoConfigPath = path.join(dependency.root, expoModuleConfigFileName);
  const expoConfig = readJsonObject(request, expoConfigPath);

  if (expoConfig === null || !lacksLinuxPlatform(expoConfig)) {
    return null;
  }

  return {
    kind: "expo-module",
    message: `${dependency.name}: not linked, ${expoConfigPath} does not list linux; Expo modules are not supported on Linux yet`,
    packageName: dependency.name,
  };
};

const referencedSourceDirectories = (request: AutolinkingRequest, cmakeListsPath: string): readonly string[] => {
  const cmakeListsDirectory = path.dirname(cmakeListsPath);
  const references =
    (request.readFile(cmakeListsPath) ?? "")
      .replaceAll(currentDirectoryVariablePattern, "")
      .match(sourceReferencePattern) ?? [];
  const directories = references.map((reference) => path.dirname(path.resolve(cmakeListsDirectory, reference)));

  return [...new Set(directories)].toSorted();
};

const findNonPortableInclude = (request: AutolinkingRequest, cmakeListsPath: string): string | null => {
  const sourcePaths = referencedSourceDirectories(request, cmakeListsPath).flatMap((directoryPath) =>
    request.listSourceFiles(directoryPath),
  );

  for (const sourcePath of sourcePaths) {
    const contents = request.readFile(sourcePath) ?? "";
    const offending = nonPortableIncludes.find(({ pattern }) => pattern.test(contents)) ?? null;

    if (offending !== null) {
      return `${sourcePath} includes ${offending.include}`;
    }
  }

  return null;
};

const linkByCxxFallback = (request: AutolinkingRequest, target: LinkTarget): AutolinkingVerdict => {
  const offending = findNonPortableInclude(request, target.cmakeListsPath);

  if (offending === null) {
    return linked("cxx-fallback", target);
  }

  return {
    kind: "rejected",
    message: `${target.packageName}: not linked, ${offending}; declare a portable build under platforms.linux in its react-native.config.js`,
    packageName: target.packageName,
  };
};

const classifyByPlatforms = (request: AutolinkingRequest, dependency: AutolinkingDependency): AutolinkingVerdict => {
  const { android = null } = dependency.platforms;
  const cmakeListsPath = android?.cxxModuleCMakeListsPath ?? null;

  if (android === null || cmakeListsPath === null) {
    return {
      kind: "no-native-code",
      message: `${dependency.name}: no native code for Linux; nothing to link`,
      packageName: dependency.name,
    };
  }

  return linkByCxxFallback(request, { cmakeListsPath, descriptor: android, packageName: dependency.name });
};

const classifyDependency = (request: AutolinkingRequest, dependency: AutolinkingDependency): AutolinkingVerdict => {
  if (request.optedOutDependencyNames.has(dependency.name)) {
    return {
      kind: "opted-out",
      message: `${dependency.name}: not linked, opted out by platforms.linux: null in ${request.applicationConfigPath}; remove that entry to link it`,
      packageName: dependency.name,
    };
  }

  const { linux = null } = dependency.platforms;

  if (linux !== null) {
    return linkExplicitly(dependency, linux);
  }

  return classifyByConfigFile(request, dependency) ?? classifyByPlatforms(request, dependency);
};

const descriptorKeys = [
  "cmakeListsPath",
  "cxxModuleCMakeListsModuleName",
  "cxxModuleCMakeListsPath",
  "cxxModuleHeaderName",
  "sourceDir",
] as const;

const toDescriptor = (value: unknown): NativeBuildDescriptor | null => {
  if (!isRecord(value)) {
    return null;
  }

  return Object.fromEntries(
    descriptorKeys.flatMap((key) => {
      const field = value[key];

      return typeof field === "string" ? [[key, field]] : [];
    }),
  );
};

const toDependency = ([name, value]: readonly [string, unknown]): readonly AutolinkingDependency[] => {
  if (!isRecord(value) || typeof value["root"] !== "string") {
    return [];
  }

  const platforms = isRecord(value["platforms"]) ? value["platforms"] : {};

  return [
    {
      name,
      platforms: { android: toDescriptor(platforms["android"]), linux: toDescriptor(platforms["linux"]) },
      root: value["root"],
    },
  ];
};

/**
 * The part of the community CLI's `react-native config` JSON discovery reads: the application root and, per
 * dependency, its root and its Android and Linux descriptors. Anything else, and any malformed entry, is ignored.
 */
const parseReactNativeConfig = (
  contents: string,
): { readonly dependencies: readonly AutolinkingDependency[]; readonly root: string } => {
  const parsed: unknown = JSON.parse(contents);

  if (!isRecord(parsed) || typeof parsed["root"] !== "string") {
    throw new TypeError("react-native config output must be a JSON object with a string root");
  }

  const dependencies = isRecord(parsed["dependencies"]) ? Object.entries(parsed["dependencies"]) : [];

  return { dependencies: dependencies.flatMap((entry) => toDependency(entry)), root: parsed["root"] };
};

/** The names an application opts out with `dependencies: { name: { platforms: { linux: null } } }`. */
const readOptedOutDependencyNames = (applicationConfig: unknown): ReadonlySet<string> => {
  const dependencies =
    isRecord(applicationConfig) && isRecord(applicationConfig["dependencies"]) ? applicationConfig["dependencies"] : {};
  const optsOut = (value: unknown): boolean =>
    isRecord(value) && isRecord(value["platforms"]) && value["platforms"]["linux"] === null;

  return new Set(Object.keys(dependencies).filter((name) => optsOut(dependencies[name])));
};

/**
 * Issue #146: every dependency the community CLI's `config` resolved gets exactly one verdict, by the rules of
 * docs/research/ecosystem-compatibility.md §4.1, in that section's order except that the opt-out is decided first,
 * because it wins over every other rule. File access goes through the request, so discovery is a pure function of
 * the dependency tree and the files it names.
 */
const discoverLinuxAutolinking = (request: AutolinkingRequest): readonly AutolinkingVerdict[] =>
  request.dependencies
    .toSorted((left, right) => left.name.localeCompare(right.name))
    .map((dependency) => classifyDependency(request, dependency));

export { discoverLinuxAutolinking, parseReactNativeConfig, readOptedOutDependencyNames };
