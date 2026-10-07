import type { AutolinkedLibrary, CodegenLibrary } from "@react-native-linux/cli/linux-autolinking-types.ts";
import {
  discoverLinuxAutolinking,
  parseReactNativeConfig,
  readOptedOutDependencyNames,
} from "@react-native-linux/cli/linux-autolinking.ts";
import { existsSync, mkdirSync, readFileSync, readdirSync, rmSync } from "node:fs";
import {
  generateAutolinkingCMake,
  generateAutolinkingRegistration,
  readCodegenConfig,
  syncGeneratedTree,
  writeFileIfChanged,
} from "@react-native-linux/cli/autolinking-cmake.ts";
import { FlowParser } from "@react-native/codegen/lib/parsers/flow/parser.js";
import type { SchemaType } from "@react-native/codegen/lib/CodegenSchema.js";
import { TypeScriptParser } from "@react-native/codegen/lib/parsers/typescript/parser.js";
import { generate } from "@react-native/codegen/lib/generators/RNCodegen.js";
import path from "node:path";
import { pathToFileURL } from "node:url";

type ReactNativeConfig = ReturnType<typeof parseReactNativeConfig>;

interface AutolinkingRun {
  readonly applicationConfigPath: string;
  readonly codegenDirectory: string;
  readonly config: ReactNativeConfig;
  readonly optedOutDependencyNames: ReadonlySet<string>;
}

const COMMAND_ARGUMENTS_START = 2;
const NO_COMPONENTS = 0;
const sourceFilePattern = /\.(?:c|cc|cpp|cxx|h|hpp|m|mm)$/u;
const specFilePattern = /^(?:Native\w+|\w+NativeComponent)\.(?:js|ts|tsx)$/u;
const flowParser = new FlowParser();
const typeScriptParser = new TypeScriptParser();

const readFileOrNull = (filePath: string): string | null =>
  existsSync(filePath) ? readFileSync(filePath, "utf8") : null;

const listSourceFiles = (directoryPath: string): readonly string[] =>
  existsSync(directoryPath)
    ? readdirSync(directoryPath)
        .filter((name) => sourceFilePattern.test(name))
        .map((name) => path.join(directoryPath, name))
    : [];

const importApplicationConfig = async (applicationConfigPath: string): Promise<unknown> => {
  if (!existsSync(applicationConfigPath)) {
    return null;
  }

  const module: unknown = await import(pathToFileURL(applicationConfigPath).href);

  return typeof module === "object" && module !== null && "default" in module ? module.default : null;
};

const readSpecModules = (specDirectory: string): SchemaType["modules"] => {
  const modules: SchemaType["modules"] = {};
  const specNames = readdirSync(specDirectory).filter((name) => specFilePattern.test(name));

  for (const specName of specNames.toSorted()) {
    const parser = specName.endsWith(".js") ? flowParser : typeScriptParser;

    Object.assign(modules, parser.parseFile(path.join(specDirectory, specName)).modules);
  }

  return modules;
};

type Schema = SchemaType["modules"][string];

const registrableComponents = (schemas: readonly Schema[]): readonly string[] =>
  schemas.flatMap((schema) =>
    schema.type === "Component"
      ? Object.entries(schema.components).flatMap(([component, shape]) =>
          shape.interfaceOnly === true ? [] : [component],
        )
      : [],
  );

const generatorsFor = (schemas: readonly Schema[]): ("componentsIOS" | "modulesCxx")[] => [
  ...(schemas.some((schema) => schema.type === "NativeModule") ? (["modulesCxx"] as const) : []),
  ...(schemas.some((schema) => schema.type === "Component") ? (["componentsIOS"] as const) : []),
];

/**
 * #149: `componentsIOS` is upstream's generator for the shared C++ props, shadow nodes, event emitters, states and
 * descriptors (core's own components use it too); a component that is not `interfaceOnly` gets a descriptor.
 */
const generateCodegen = (packageRoot: string, codegenDirectory: string): CodegenLibrary | null => {
  const packageJson = readFileOrNull(path.join(packageRoot, "package.json"));
  const codegenConfig = packageJson === null ? null : readCodegenConfig(packageJson);

  if (codegenConfig === null) {
    return null;
  }

  const { jsSourceDirectory, name } = codegenConfig;
  const modules = readSpecModules(path.join(packageRoot, jsSourceDirectory));
  const schemas = Object.values(modules);

  generate(
    {
      assumeNonnull: false,
      libraryName: name,
      outputDirectory: path.join(codegenDirectory, name),
      packageName: name,
      schema: { modules },
    },
    { generators: generatorsFor(schemas) },
  );

  return { components: registrableComponents(schemas), name };
};

const autolinkedLibraries = (run: AutolinkingRun): readonly AutolinkedLibrary[] =>
  discoverLinuxAutolinking({
    applicationConfigPath: run.applicationConfigPath,
    dependencies: run.config.dependencies,
    listSourceFiles,
    optedOutDependencyNames: run.optedOutDependencyNames,
    readFile: readFileOrNull,
  }).flatMap((verdict) => {
    process.stdout.write(`${verdict.message}\n`);

    return verdict.kind === "linked" ? [verdict] : [];
  });

const codegenLibraries = (run: AutolinkingRun): readonly CodegenLibrary[] =>
  run.config.dependencies.flatMap(({ name, root }) => {
    const codegen = run.optedOutDependencyNames.has(name) ? null : generateCodegen(root, run.codegenDirectory);

    if (codegen !== null && codegen.components.length > NO_COMPONENTS) {
      process.stdout.write(`${name}: components linked by codegen: ${codegen.components.join(", ")}\n`);
    }

    return codegen === null ? [] : [codegen];
  });

const [configPath = null, outputDirectory = null] = process.argv.slice(COMMAND_ARGUMENTS_START);

if (configPath === null || outputDirectory === null) {
  throw new Error("Usage: node scripts/autolink.ts <react-native config JSON> <output directory>");
}

const config = parseReactNativeConfig(readFileSync(configPath, "utf8"));
const applicationConfigPath = path.join(config.root, "react-native.config.js");
const codegenDirectory = path.resolve(outputDirectory, "codegen");
const optedOutDependencyNames = readOptedOutDependencyNames(await importApplicationConfig(applicationConfigPath));

/*
 * Codegen writes every file it generates on every run, so it writes into a staging directory and only what changed
 * reaches the tree CMake reads: an unrelated edit leaves every output's mtime alone (#147).
 */
const stagingDirectory = path.resolve(outputDirectory, ".codegen-staging");

rmSync(stagingDirectory, { force: true, recursive: true });
mkdirSync(stagingDirectory, { recursive: true });

const run = { applicationConfigPath, codegenDirectory: stagingDirectory, config, optedOutDependencyNames };
const libraries = autolinkedLibraries(run);
const codegen = codegenLibraries(run);

syncGeneratedTree(stagingDirectory, codegenDirectory);
rmSync(stagingDirectory, { force: true, recursive: true });
writeFileIfChanged(
  path.resolve(outputDirectory, "rnl_autolinking.cmake"),
  generateAutolinkingCMake(libraries, codegen, codegenDirectory),
);
writeFileIfChanged(
  path.resolve(outputDirectory, "rnl_autolinking.cpp"),
  generateAutolinkingRegistration(libraries, codegen),
);
