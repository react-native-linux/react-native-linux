import { argv, stdout } from "node:process";

import { createPackage } from "./create-package/create-package-command.ts";
import { createUpstreamEnvironment } from "./upstream/upstream-environment.ts";
import path from "node:path";

const COMMAND_ARGUMENTS_START = 2;

const repositoryRoot = path.resolve(import.meta.dirname, "..");
const result = createPackage(argv.slice(COMMAND_ARGUMENTS_START), repositoryRoot, createUpstreamEnvironment());

for (const message of result.messages) {
  stdout.write(`${message}\n`);
}

process.exitCode = result.exitCode;
