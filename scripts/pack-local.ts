import { cpSync, existsSync, mkdirSync, rmSync, writeFileSync } from "node:fs";
import { argv } from "node:process";
import { execFileSync } from "node:child_process";
import path from "node:path";
import { planCoreBinary } from "./pack-local/core-binary-plan.ts";

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

const plan = planCoreBinary(buildDirectory, path.join(coreDirectory, "fonts"), binDirectory);
const missing = plan.filter((copy) => !existsSync(copy.from)).map((copy) => copy.from);

if (missing.length > NOTHING_MISSING) {
  throw new Error(`pack:local needs a release build and vendored fonts; missing:\n${missing.join("\n")}`);
}

rmSync(path.join(cliDirectory, "dist"), { force: true, recursive: true });
execFileSync("pnpm", ["exec", "tsc", "-p", path.join(cliDirectory, "tsconfig.pack.json")], { stdio: "inherit" });

rmSync(binDirectory, { force: true, recursive: true });
mkdirSync(binDirectory, { recursive: true });

for (const copy of plan) {
  cpSync(copy.from, copy.to, { recursive: true });
}

writeFileSync(path.join(binDirectory, "portable.marker"), "");
rmSync(packDirectory, { force: true, recursive: true });

for (const packageDirectory of [cliDirectory, coreDirectory]) {
  execFileSync("pnpm", ["pack", "--pack-destination", packDirectory], { cwd: packageDirectory, stdio: "inherit" });
}
