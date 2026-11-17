import { mkdirSync, realpathSync, writeFileSync } from "node:fs";
import { androidDependencyConfig } from "@react-native-linux/cli/android-dependency-config.ts";
import { execPath } from "node:process";
import path from "node:path";
import { pathToFileURL } from "node:url";
import { spawnSync } from "node:child_process";

/*
 * The autolinking of the libraries the test harness installs, for the rnl_window the e2e scenarios run (#806):
 *
 *   node scripts/autolink-harness.ts build/autolinking-harness
 *   cmake --preset dev -DRNL_AUTOLINKING_CMAKE=$PWD/build/autolinking-harness/rnl_autolinking.cmake
 *
 * It writes the `react-native config` JSON `scripts/autolink.ts` reads, for each library below as the harness has
 * it installed, and runs that script on it. `--dependency <library root>` prints one library's entry instead, which
 * is how CI's fabric job autolinks the registry tarballs it unpacks.
 */

type DependencyConfig = ReturnType<typeof androidDependencyConfig>;

const COMMAND_ARGUMENTS_START = 2;
const JSON_INDENTATION = 2;
const FAILURE_EXIT_STATUS = 1;
const HARNESS_LIBRARIES = ["react-native-safe-area-context"];

const repositoryRoot = path.resolve(import.meta.dirname, "..");
const harnessRoot = path.join(repositoryRoot, "packages", "test-harness");

const dependencyConfigOf = async (libraryRoot: string): Promise<DependencyConfig> => {
  const configUrl = pathToFileURL(path.join(libraryRoot, "react-native.config.js")).href;
  const module: unknown = await import(configUrl);

  return androidDependencyConfig(
    libraryRoot,
    typeof module === "object" && module !== null && "default" in module ? module.default : null,
  );
};

const autolinkHarness = async (outputDirectory: string): Promise<number> => {
  const configPath = path.join(outputDirectory, "react-native-config.json");
  const entries = await Promise.all(
    HARNESS_LIBRARIES.map(async (name) => {
      const libraryRoot = realpathSync(path.join(harnessRoot, "node_modules", name));

      return [name, await dependencyConfigOf(libraryRoot)] as const;
    }),
  );
  const config = { dependencies: Object.fromEntries(entries), root: harnessRoot };

  mkdirSync(outputDirectory, { recursive: true });
  writeFileSync(configPath, `${JSON.stringify(config, null, JSON_INDENTATION)}\n`);

  const autolink = spawnSync(
    execPath,
    [path.join(repositoryRoot, "scripts", "autolink.ts"), configPath, outputDirectory],
    { stdio: "inherit" },
  );

  return autolink.status ?? FAILURE_EXIT_STATUS;
};

const [first = null, second = null] = process.argv.slice(COMMAND_ARGUMENTS_START);

if (first === "--dependency" && second !== null) {
  const entry = await dependencyConfigOf(path.resolve(second));

  process.stdout.write(`${JSON.stringify(entry)}\n`);
} else if (first !== null && second === null) {
  process.exitCode = await autolinkHarness(path.resolve(first));
} else {
  throw new Error("Usage: node scripts/autolink-harness.ts <output directory> | --dependency <library root>");
}
