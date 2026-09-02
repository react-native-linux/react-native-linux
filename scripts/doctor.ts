import { argv, env, stdout } from "node:process";
import { computeExitCode, evaluateChecks } from "./doctor/evaluate-check.ts";
import { existsSync, readFileSync, readdirSync } from "node:fs";
import { findLavapipeIcdManifestPath, findLavapipeLibraryPath } from "./window-golden.ts";
import { formatJsonReport, formatReport, parseDistroId } from "./doctor/format-report.ts";

import type { Tier } from "./doctor/doctor-check.ts";
import { doctorChecks } from "./doctor/doctor-checks.ts";
import { spawnSync } from "node:child_process";

const NOT_FOUND_INDEX = -1;
const NEXT_ARGUMENT = 1;
const NO_TIERS_REQUESTED = 0;

const OS_RELEASE_PATH = "/etc/os-release";
const DISTRO_FLAG = "--distro";
const JSON_FLAG = "--json";

const ALL_TIERS: readonly Tier[] = ["core", "window", "goldens", "coverage"];
const TIER_FLAGS: Readonly<Record<Tier, string>> = {
  core: "--core",
  coverage: "--coverage",
  goldens: "--goldens",
  window: "--window",
};

type EnvironmentParameterOf<FunctionType> = FunctionType extends (
  checks: infer _Checks,
  environment: infer Environment,
  tiers: infer _Tiers,
) => unknown
  ? Environment
  : never;

type RealProbeEnvironment = EnvironmentParameterOf<typeof evaluateChecks>;

const runCommand: RealProbeEnvironment["runCommand"] = (executableName, commandArguments) => {
  const result = spawnSync(executableName, [...commandArguments], { encoding: "utf8" });

  return result.error ? null : { exitCode: result.status, output: `${result.stdout}${result.stderr}` };
};

const listDirectoryEntries: RealProbeEnvironment["listDirectoryEntries"] = (directoryPath) =>
  existsSync(directoryPath) ? readdirSync(directoryPath) : [];

const readEnvironmentVariable: RealProbeEnvironment["readEnvironmentVariable"] = (variableName) =>
  env[variableName] ?? null;

const hasLavapipeIcd = (): boolean => findLavapipeIcdManifestPath() !== null || findLavapipeLibraryPath() !== null;

const buildRealEnvironment = (): RealProbeEnvironment => ({
  hasLavapipeIcd,
  listDirectoryEntries,
  readEnvironmentVariable,
  runCommand,
});

const readOsReleaseContent = (): string => (existsSync(OS_RELEASE_PATH) ? readFileSync(OS_RELEASE_PATH, "utf8") : "");

const readDistroFlagValue = (): string | null => {
  const flagIndex = argv.indexOf(DISTRO_FLAG);

  if (flagIndex === NOT_FOUND_INDEX) {
    return null;
  }

  return argv[flagIndex + NEXT_ARGUMENT] ?? null;
};

const resolveDistro = (): ReturnType<typeof parseDistroId> => {
  const flagValue = readDistroFlagValue();

  if (flagValue === "arch" || flagValue === "ubuntu") {
    return flagValue;
  }

  return parseDistroId(readOsReleaseContent());
};

const readSelectedTiers = (): readonly Tier[] => {
  const requestedTiers = ALL_TIERS.filter((tier) => argv.includes(TIER_FLAGS[tier]));

  return requestedTiers.length > NO_TIERS_REQUESTED ? requestedTiers : ALL_TIERS;
};

const results = evaluateChecks(doctorChecks, buildRealEnvironment(), readSelectedTiers());
const exitCode = computeExitCode(results);

stdout.write(
  argv.includes(JSON_FLAG) ? `${formatJsonReport(results, exitCode)}\n` : `${formatReport(results, resolveDistro())}\n`,
);

process.exitCode = exitCode;
