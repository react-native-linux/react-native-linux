import { cpSync, readdirSync, rmSync } from "node:fs";
import { isLinuxVariant, overlayInstallArguments, overlayPackArguments } from "./e2e/t3code.ts";
import { packageAliases } from "@react-native-linux/cli/package-aliases.ts";
import path from "node:path";
import { spawnSync } from "node:child_process";

const SUCCESSFUL_EXIT_STATUS = 0;

/** The output of pnpm run in `directory` when it fails, or null when it succeeds. */
const runPnpm = (pnpmArguments: readonly string[], directory: string): string | null => {
  const run = spawnSync("pnpm", pnpmArguments, { cwd: directory, encoding: "utf8" });

  return run.status === SUCCESSFUL_EXIT_STATUS ? null : `${run.stdout}${run.stderr}`;
};

/** Every overlay package the alias table names, packed and added to the mobile app; the failure, or null. */
const installOverlays = (repositoryRoot: string, overlayDirectory: string, checkout: string): string | null => {
  rmSync(overlayDirectory, { force: true, recursive: true });

  return (
    runPnpm(overlayPackArguments(packageAliases, overlayDirectory), repositoryRoot) ??
    runPnpm(
      overlayInstallArguments(readdirSync(overlayDirectory).map((name) => path.join(overlayDirectory, name))),
      checkout,
    )
  );
};

/**
 * The Linux variants of T3 Code's own files (#707), copied beside the files they replace as T3 Code would carry them,
 * so Metro picks each by its platform extension and its relative imports reach T3 Code's own modules.
 */
const installVariants = (variantsDirectory: string, mobileRoot: string): void => {
  const variants = readdirSync(variantsDirectory, { encoding: "utf8", recursive: true }).filter((relativePath) =>
    isLinuxVariant(relativePath),
  );

  for (const variant of variants) {
    cpSync(path.join(variantsDirectory, variant), path.join(mobileRoot, "src", variant));
  }
};

export { embedExpoAppAssets } from "@react-native-linux/cli/expo-app-assets.ts";
export { installOverlays, installVariants };
