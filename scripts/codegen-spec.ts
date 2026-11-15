import { FlowParser } from "@react-native/codegen/lib/parsers/flow/parser.js";
import { TypeScriptParser } from "@react-native/codegen/lib/parsers/typescript/parser.js";
import { generate } from "@react-native/codegen/lib/generators/RNCodegen.js";

const COMMAND_ARGUMENTS_START = 2;
const [specPath = null, libraryName = null, outputDirectory = null, generator = null] =
  process.argv.slice(COMMAND_ARGUMENTS_START);

if (
  specPath === null ||
  libraryName === null ||
  outputDirectory === null ||
  (generator !== "modulesCxx" && generator !== "componentsIOS")
) {
  throw new Error(
    "Usage: node scripts/codegen-spec.ts <Flow or TypeScript spec> <library name> <output directory> <modulesCxx|componentsIOS>",
  );
}

const parser = specPath.endsWith(".ts") ? new TypeScriptParser() : new FlowParser();

generate(
  {
    assumeNonnull: false,
    libraryName,
    outputDirectory,
    packageName: libraryName,
    schema: parser.parseFile(specPath),
  },
  { generators: [generator] },
);
