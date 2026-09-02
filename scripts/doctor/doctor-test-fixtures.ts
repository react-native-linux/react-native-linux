import type { CommandResult, ProbeEnvironment } from "./evaluate-check.ts";
import type { DoctorCheck } from "./doctor-check.ts";

const buildCheck = (overrides: Partial<DoctorCheck>): DoctorCheck => ({
  name: "fixture check",
  probe: { kind: "env-var", variableName: "FIXTURE_VARIABLE" },
  remedy: { arch: "fixture arch remedy", ubuntu: "fixture ubuntu remedy" },
  required: true,
  tier: "core",
  why: "fixture why",
  ...overrides,
});

const buildEnvironment = (overrides: Partial<ProbeEnvironment>): ProbeEnvironment => ({
  hasLavapipeIcd: () => false,
  listDirectoryEntries: () => [],
  readEnvironmentVariable: () => null,
  runCommand: () => null,
  ...overrides,
});

const buildCommandResult = (output: string, exitCode: number): CommandResult => ({ exitCode, output });

export { buildCheck, buildCommandResult, buildEnvironment };
