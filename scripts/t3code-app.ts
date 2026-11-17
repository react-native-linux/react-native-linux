import { CHECKOUT_INSTALL_ARGUMENTS, overlayInstallArguments, overlayPackArguments } from "./e2e/t3code.ts";
import { cropImage, findScreenshotFailure } from "./e2e/screenshot.ts";
import { existsSync, readFileSync, readdirSync, rmSync, writeFileSync } from "node:fs";
import { PNG } from "pngjs";
import { env } from "node:process";
import { packageAliases } from "@react-native-linux/cli/package-aliases.ts";
import path from "node:path";
import { spawnSync } from "node:child_process";

const SUCCESSFUL_EXIT_STATUS = 0;
/** T3 Code's main screen above LogBox's toast, whose warning count varies from run to run. */
const MAIN_SCREEN_CROP = { height: 640, left: 0, top: 0, width: 1280 };
const MAX_DIFFERENT_PIXELS = 0;
const goldenPath = path.join(
  import.meta.dirname,
  "..",
  "packages",
  "test-harness",
  "e2e",
  "goldens",
  "t3code-main-screen.png",
);

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

/**
 * Why the screenshot's main screen does not match the golden: empty when it does. `RNL_UPDATE_GOLDENS=1` writes the
 * golden from it first.
 */
/**
 * Why the screenshot, cropped to the main screen, does not match the golden, or null. `RNL_UPDATE_GOLDENS=1 pnpm
 * e2e:t3code` regenerates the golden from it first.
 */
const findGoldenMismatch = (screenshotPath: string): string | null => {
  const cropped = cropImage(PNG.sync.read(readFileSync(screenshotPath)), MAIN_SCREEN_CROP);

  if (typeof cropped === "string") {
    return cropped;
  }

  if (env["RNL_UPDATE_GOLDENS"] === "1") {
    const golden = new PNG({ height: cropped.height, width: cropped.width });

    golden.data.set(cropped.data);
    writeFileSync(goldenPath, PNG.sync.write(golden));
  }

  return existsSync(goldenPath)
    ? findScreenshotFailure(cropped, PNG.sync.read(readFileSync(goldenPath)), MAX_DIFFERENT_PIXELS)
    : "it is missing; regenerate it with RNL_UPDATE_GOLDENS=1";
};

const findMainScreenMismatches = (screenshotPath: string): readonly string[] => {
  const mismatch = findGoldenMismatch(screenshotPath);

  return mismatch === null ? [] : [`the main screen does not match ${path.basename(goldenPath)}: ${mismatch}`];
};

export { embedExpoAppAssets } from "@react-native-linux/cli/expo-app-assets.ts";
export { findMainScreenMismatches, installOverlays };
