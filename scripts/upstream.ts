import { argv, stdout } from "node:process";

import { createUpstreamEnvironment } from "./upstream/upstream-environment.ts";
import path from "node:path";
import { runUpstreamCommand } from "./upstream/upstream-commands.ts";

const COMMAND_ARGUMENTS_START = 2;

const repositoryRoot = path.resolve(import.meta.dirname, "..");
const result = runUpstreamCommand(argv.slice(COMMAND_ARGUMENTS_START), repositoryRoot, createUpstreamEnvironment());

for (const message of result.messages) {
  stdout.write(`${message}\n`);
}

process.exitCode = result.exitCode;
