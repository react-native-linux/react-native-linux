import { argv, execPath } from "node:process";
import { copyFileSync, existsSync, mkdirSync, mkdtempSync, readFileSync, rmSync } from "node:fs";
import { findExecutable, findLavapipeIcd } from "./window-golden.ts";
import { execFileSync } from "node:child_process";
import path from "node:path";
import { pathToFileURL } from "node:url";
import { runUnderCompositor } from "./headless-compositor.ts";
import { tmpdir } from "node:os";

const COMMUNITY_CLI = "@react-native-community/cli@20.2.0";
const APPLICATION_NAME = "FreshApp";
const FRAME_COUNT = "120";
const RENDER_TIMEOUT_MS = 120_000;
const UPDATE_FLAG = "--update";
const COMMAND_ARGUMENTS_START = 2;
const MODULE_FLAG_PATTERN = /--module (?<key>\S+)/u;

const repositoryRoot = path.resolve(import.meta.dirname, "..");
const [buildArgument] = argv.slice(COMMAND_ARGUMENTS_START).filter((argument) => argument !== UPDATE_FLAG);
const buildDirectory = path.resolve(buildArgument ?? path.join(repositoryRoot, "build", "release"));
const packDirectory = path.join(repositoryRoot, "build", "pack");
const outputDirectory = path.join(repositoryRoot, "build", "fresh-project");
const renderedPath = path.join(outputDirectory, "fresh-project.png");
const goldenPath = path.join(repositoryRoot, "packages", "core", "goldens", "fresh-project.png");

const run = (command: string, commandArguments: readonly string[], cwd: string): void => {
  execFileSync(command, commandArguments, { cwd, stdio: "inherit" });
};

/** The headless rig, or a loud failure: this smoke test never skips. */
const requireRig = (): { readonly compositorPath: string; readonly lavapipeIcdPath: string } => {
  const compositorPath = findExecutable("weston");
  const lavapipeIcdPath = findLavapipeIcd();

  if (compositorPath === null || lavapipeIcdPath === null) {
    throw new Error(
      "the fresh-project smoke test needs weston and the lavapipe ICD; install weston and mesa-vulkan-drivers",
    );
  }

  return { compositorPath, lavapipeIcdPath };
};

const readLinuxScript = (applicationDirectory: string): string => {
  const manifest: unknown = JSON.parse(readFileSync(path.join(applicationDirectory, "package.json"), "utf8"));

  if (typeof manifest !== "object" || manifest === null || !("scripts" in manifest)) {
    return "";
  }

  const { scripts } = manifest;

  return typeof scripts === "object" && scripts !== null && "linux" in scripts && typeof scripts.linux === "string"
    ? scripts.linux
    : "";
};

/**
 * The `AppRegistry` key the scaffolded `linux` script hands rnl_window (#777), which init must have renamed from the
 * template's `HelloWorld` to the project's name, exactly as it renames the key `index.tsx` registers.
 */
const readModuleKey = (applicationDirectory: string): string => {
  const linuxScript = readLinuxScript(applicationDirectory);
  const moduleKey = MODULE_FLAG_PATTERN.exec(linuxScript)?.groups?.["key"];

  if (moduleKey !== APPLICATION_NAME) {
    throw new Error(`the scaffolded linux script "${linuxScript}" does not run --module ${APPLICATION_NAME}`);
  }

  return moduleKey;
};

/** The scaffolded app's own rnl_window, from the packed core, rendering its own bundle under the headless rig. */
const renderApplication = async (applicationDirectory: string): Promise<void> => {
  const moduleKey = readModuleKey(applicationDirectory);
  const { compositorPath, lavapipeIcdPath } = requireRig();

  await runUnderCompositor(compositorPath, lavapipeIcdPath, (clientEnvironment) => {
    execFileSync(
      path.join(applicationDirectory, "node_modules", ".bin", "rnl_window"),
      [
        "--fabric",
        path.join(applicationDirectory, "build", "index.linux.bundle.js"),
        "--module",
        moduleKey,
        "--screenshot",
        renderedPath,
        "--frames",
        FRAME_COUNT,
        "--no-decorations",
      ],
      { env: clientEnvironment, stdio: "inherit", timeout: RENDER_TIMEOUT_MS },
    );

    if (!existsSync(renderedPath)) {
      throw new Error(`rnl_window exited without writing ${renderedPath}`);
    }
  });
};

const scratchDirectory = mkdtempSync(path.join(tmpdir(), "rnl-fresh-app-"));
const applicationDirectory = path.join(scratchDirectory, APPLICATION_NAME);

rmSync(outputDirectory, { force: true, recursive: true });
mkdirSync(outputDirectory, { recursive: true });

try {
  run(execPath, [path.join(repositoryRoot, "scripts", "pack-local.ts"), buildDirectory], repositoryRoot);
  run(
    "npx",
    [
      "--yes",
      COMMUNITY_CLI,
      "init",
      APPLICATION_NAME,
      "--template",
      pathToFileURL(path.join(repositoryRoot, "packages", "template")).href,
      "--skip-install",
      "--skip-git-init",
    ],
    scratchDirectory,
  );
  run(
    "pnpm",
    [
      "add",
      "-D",
      path.join(packDirectory, "react-native-linux-cli-0.0.0.tgz"),
      path.join(packDirectory, "react-native-linux-core-0.0.0.tgz"),
    ],
    applicationDirectory,
  );
  run("pnpm", ["bundle:linux"], applicationDirectory);
  await renderApplication(applicationDirectory);
} finally {
  rmSync(scratchDirectory, { force: true, recursive: true });
}

if (argv.includes(UPDATE_FLAG)) {
  copyFileSync(renderedPath, goldenPath);
}
