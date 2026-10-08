import { FlowParser } from "@react-native/codegen/lib/parsers/flow/parser.js";
import { generate } from "@react-native/codegen/lib/generators/RNCodegen.js";

const COMMAND_ARGUMENTS_START = 2;
const [specPath = null, libraryName = null, outputDirectory = null] = process.argv.slice(COMMAND_ARGUMENTS_START);

if (specPath === null || libraryName === null || outputDirectory === null) {
  throw new Error("Usage: node scripts/codegen-cxx-module.ts <Flow spec> <library name> <output directory>");
}

generate(
  {
    assumeNonnull: false,
    libraryName,
    outputDirectory,
    packageName: libraryName,
    schema: new FlowParser().parseFile(specPath),
  },
  { generators: ["modulesCxx"] },
);
