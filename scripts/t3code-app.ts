import { CHECKOUT_INSTALL_ARGUMENTS, overlayInstallArguments, overlayPackArguments } from "./e2e/t3code.ts";
import { readdirSync, rmSync } from "node:fs";
import { packageAliases } from "@react-native-linux/cli/package-aliases.ts";
import path from "node:path";
import { spawnSync } from "node:child_process";

const SUCCESSFUL_EXIT_STATUS = 0;

/** The output of pnpm run in `directory` when it fails, or null when it succeeds. */
const runPnpm = (pnpmArguments: readonly string[], directory: string): string | null => {
  const run = spawnSync("pnpm", pnpmArguments, { cwd: directory, encoding: "utf8" });

  return run.status === SUCCESSFUL_EXIT_STATUS ? null : `${run.stdout}${run.stderr}`;
};

/**
 * Every overlay package the alias table names, packed and added to the mobile app, with the checkout's own install run
 * again after the add so the workspace packages the mobile app depends on stay linked; the failure, or null.
 */
const installOverlays = (repositoryRoot: string, overlayDirectory: string, checkout: string): string | null => {
  rmSync(overlayDirectory, { force: true, recursive: true });

  return (
    runPnpm(overlayPackArguments(packageAliases, overlayDirectory), repositoryRoot) ??
    runPnpm(
      overlayInstallArguments(readdirSync(overlayDirectory).map((name) => path.join(overlayDirectory, name))),
      checkout,
    ) ??
    runPnpm(CHECKOUT_INSTALL_ARGUMENTS, checkout)
  );
};

export { embedExpoAppAssets } from "@react-native-linux/cli/expo-app-assets.ts";
export { installOverlays };
