import {
  discoverLinuxAutolinking,
  parseReactNativeConfig,
  readOptedOutDependencyNames,
} from "@react-native-linux/cli/linux-autolinking.ts";
import { existsSync, mkdirSync, readFileSync, readdirSync, writeFileSync } from "node:fs";
import {
  generateAutolinkingCMake,
  generateAutolinkingRegistration,
  readModuleCodegenConfig,
} from "@react-native-linux/cli/autolinking-cmake.ts";
import type { AutolinkedLibrary } from "@react-native-linux/cli/linux-autolinking-types.ts";
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
const sourceFilePattern = /\.(?:c|cc|cpp|cxx|h|hpp|m|mm)$/u;
const specFilePattern = /^Native\w+\.(?:js|ts|tsx)$/u;
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

const generateModuleCodegen = (packageRoot: string, codegenDirectory: string): string | null => {
  const packageJson = readFileOrNull(path.join(packageRoot, "package.json"));
  const codegenConfig = packageJson === null ? null : readModuleCodegenConfig(packageJson);

  if (codegenConfig === null) {
    return null;
  }

  const { jsSourceDirectory, name } = codegenConfig;
  const schema = { modules: readSpecModules(path.join(packageRoot, jsSourceDirectory)) };
  const outputDirectory = path.join(codegenDirectory, name);

  generate(
    { assumeNonnull: false, libraryName: name, outputDirectory, packageName: name, schema },
    { generators: ["modulesCxx"] },
  );

  return name;
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

    const root = run.config.dependencies.find(({ name }) => name === verdict.packageName)?.root ?? null;

    return verdict.kind === "linked" && root !== null
      ? [{ ...verdict, codegenName: generateModuleCodegen(root, run.codegenDirectory) }]
      : [];
  });

const [configPath = null, outputDirectory = null] = process.argv.slice(COMMAND_ARGUMENTS_START);

if (configPath === null || outputDirectory === null) {
  throw new Error("Usage: node scripts/autolink.ts <react-native config JSON> <output directory>");
}

const config = parseReactNativeConfig(readFileSync(configPath, "utf8"));
const applicationConfigPath = path.join(config.root, "react-native.config.js");
const codegenDirectory = path.resolve(outputDirectory, "codegen");
const optedOutDependencyNames = readOptedOutDependencyNames(await importApplicationConfig(applicationConfigPath));

mkdirSync(codegenDirectory, { recursive: true });

const libraries = autolinkedLibraries({ applicationConfigPath, codegenDirectory, config, optedOutDependencyNames });

writeFileSync(
  path.resolve(outputDirectory, "rnl_autolinking.cmake"),
  generateAutolinkingCMake(libraries, codegenDirectory),
);
writeFileSync(path.resolve(outputDirectory, "rnl_autolinking.cpp"), generateAutolinkingRegistration(libraries));
