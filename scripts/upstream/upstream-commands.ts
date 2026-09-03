import type { CommandResult, UpstreamEnvironment } from "./upstream-types.ts";

import { bumpLibrary, vendorLibrary } from "./upstream-vendor.ts";
import { capturePatch } from "./upstream-patch.ts";
import { checkLibraries } from "./upstream-check.ts";
import { usageResult } from "./upstream-library.ts";

type CommandHandler = (
  commandArguments: readonly string[],
  repositoryRoot: string,
  environment: UpstreamEnvironment,
) => CommandResult;

const COMMAND_ARGUMENT_INDEX = 0;

const COMMAND_HANDLERS: Readonly<Record<string, CommandHandler>> = {
  bump: bumpLibrary,
  check: checkLibraries,
  patch: capturePatch,
  vendor: vendorLibrary,
};

const runUpstreamCommand = (
  commandArguments: readonly string[],
  repositoryRoot: string,
  environment: UpstreamEnvironment,
): CommandResult => {
  const handler = COMMAND_HANDLERS[commandArguments[COMMAND_ARGUMENT_INDEX] ?? ""] ?? null;

  return handler === null ? usageResult() : handler(commandArguments, repositoryRoot, environment);
};

export { runUpstreamCommand };
