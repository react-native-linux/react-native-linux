import type { CoreCodegenEnvironment, CoreCodegenPaths } from "./codegen-core/generate-core-artifacts-types.ts";
import { existsSync, mkdirSync, readFileSync, readdirSync, rmSync, unlinkSync, writeFileSync } from "node:fs";

import { FlowParser } from "@react-native/codegen/lib/parsers/flow/parser.js";
import { generate } from "@react-native/codegen/lib/generators/RNCodegen.js";
import path from "node:path";
import { runCoreCodegen } from "./codegen-core/generate-core-artifacts.ts";

const scriptDirectory = import.meta.dirname;
const repositoryRoot = path.resolve(scriptDirectory, "..");
const flowParser = new FlowParser();

const paths: CoreCodegenPaths = {
  codegenPackageJsonPath: path.join(repositoryRoot, "node_modules", "@react-native", "codegen", "package.json"),
  outputDirectory: path.join(repositoryRoot, "packages", "core", "generated"),
  repositoryRoot,
  specSourceDirectory: path.join(repositoryRoot, "third_party", "react-native", "packages", "react-native", "src"),
};

const environment: CoreCodegenEnvironment = {
  generateArtifacts: generate,
  listDirectory: (directoryPath) =>
    readdirSync(directoryPath, { withFileTypes: true }).map((entry) => ({
      isDirectory: entry.isDirectory(),
      name: entry.name,
    })),
  parseSpecFile: (filePath) => flowParser.parseFile(filePath),
  readFile: (filePath) => readFileSync(filePath, "utf8"),
  removeFile: unlinkSync,
  report: (message) => {
    process.stdout.write(`${message}\n`);
  },
  resetDirectory: (directoryPath) => {
    rmSync(directoryPath, { force: true, recursive: true });
    mkdirSync(directoryPath, { recursive: true });
  },
  writeFile: writeFileSync,
};

if (!existsSync(paths.specSourceDirectory)) {
  throw new Error(`${paths.specSourceDirectory} is missing. Run: node scripts/vendor-react-native.ts`);
}

runCoreCodegen(paths, environment);
