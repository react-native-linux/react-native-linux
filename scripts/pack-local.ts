import { cpSync, existsSync, mkdirSync, rmSync, writeFileSync } from "node:fs";
import { argv } from "node:process";
import { execFileSync } from "node:child_process";
import path from "node:path";
import { planCoreBinary } from "./pack-local/core-binary-plan.ts";
import { shippedLibraries } from "@react-native-linux/cli/dependency-floor.ts";

const BUILD_DIRECTORY_ARGUMENT_INDEX = 2;
const NOTHING_MISSING = 0;

const repositoryRoot = path.resolve(import.meta.dirname, "..");
const buildDirectory = path.resolve(
  argv[BUILD_DIRECTORY_ARGUMENT_INDEX] ?? path.join(repositoryRoot, "build", "release"),
);
const cliDirectory = path.join(repositoryRoot, "packages", "cli");
const coreDirectory = path.join(repositoryRoot, "packages", "core");
const binDirectory = path.join(coreDirectory, "bin");
const packDirectory = path.join(repositoryRoot, "build", "pack");
const binaryPath = path.join(buildDirectory, "bin", "rnl_window");
const fontDirectory = path.join(coreDirectory, "fonts");

const missing = [binaryPath, fontDirectory].filter((requiredPath) => !existsSync(requiredPath));

if (missing.length > NOTHING_MISSING) {
  throw new Error(`pack:local needs a release build and vendored fonts; missing:\n${missing.join("\n")}`);
}

const plan = planCoreBinary({
  binDirectory,
  binaryPath,
  fontDirectory,
  lddOutput: execFileSync("ldd", [binaryPath], { encoding: "utf8" }),
  shippedLibraries,
});

rmSync(path.join(cliDirectory, "dist"), { force: true, recursive: true });
execFileSync("pnpm", ["exec", "tsc", "-p", path.join(cliDirectory, "tsconfig.pack.json")], { stdio: "inherit" });

rmSync(binDirectory, { force: true, recursive: true });
mkdirSync(binDirectory, { recursive: true });

for (const copy of plan) {
  cpSync(copy.from, copy.to, { dereference: true, recursive: true });
}

writeFileSync(path.join(binDirectory, "portable.marker"), "");
rmSync(packDirectory, { force: true, recursive: true });

for (const packageDirectory of [cliDirectory, coreDirectory]) {
  execFileSync("pnpm", ["pack", "--pack-destination", packDirectory], { cwd: packageDirectory, stdio: "inherit" });
}
