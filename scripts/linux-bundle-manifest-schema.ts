import { argv, stderr, stdout } from "node:process";
import {
  describeManifestSchemaDrift,
  renderManifestSchemaJson,
} from "@react-native-linux/cli/manifest-schema-drift.ts";
import { existsSync, mkdirSync, readFileSync, writeFileSync } from "node:fs";

import path from "node:path";

const COMMAND_ARGUMENT_INDEX = 2;
const FAILURE_EXIT_STATUS = 1;

const SCHEMA_PATH = "packages/cli/schemas/linux-bundle-manifest.schema.json";

const repositoryRoot = path.resolve(import.meta.dirname, "..");
const schemaAbsolutePath = path.join(repositoryRoot, SCHEMA_PATH);

const runGenerate = (): void => {
  mkdirSync(path.dirname(schemaAbsolutePath), { recursive: true });
  writeFileSync(schemaAbsolutePath, renderManifestSchemaJson());
  stdout.write(`wrote ${SCHEMA_PATH}\n`);
};

const runCheck = (): void => {
  const onDiskContents = existsSync(schemaAbsolutePath) ? readFileSync(schemaAbsolutePath, "utf8") : null;
  const drift = describeManifestSchemaDrift(onDiskContents);

  if (drift === null) {
    stdout.write(`${SCHEMA_PATH}: no drift\n`);
    return;
  }

  stderr.write(`${drift}\n`);
  process.exitCode = FAILURE_EXIT_STATUS;
};

const command = argv[COMMAND_ARGUMENT_INDEX];

if (command === "check") {
  runCheck();
} else if (command === "generate") {
  runGenerate();
} else {
  stderr.write("linux-bundle-manifest-schema: expected a command of check or generate\n");
  process.exitCode = FAILURE_EXIT_STATUS;
}
