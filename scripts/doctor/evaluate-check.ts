import type {
  BinaryProbe,
  DoctorCheck,
  EnvVarProbe,
  FileGlobProbe,
  PkgConfigProbe,
  Probe,
  Tier,
} from "./doctor-check.ts";

interface CommandResult {
  readonly exitCode: number | null;
  readonly output: string;
}

interface ProbeEnvironment {
  readonly hasLavapipeIcd: () => boolean;
  readonly listDirectoryEntries: (directoryPath: string) => readonly string[];
  readonly readEnvironmentVariable: (variableName: string) => string | null;
  readonly runCommand: (executableName: string, commandArguments: readonly string[]) => CommandResult | null;
}

type CheckOutcome = "fail" | "pass";

interface ProbeEvaluation {
  readonly detail: string;
  readonly outcome: CheckOutcome;
}

interface CheckResult extends ProbeEvaluation {
  readonly check: DoctorCheck;
}

interface FoundBinary {
  readonly binaryName: string;
  readonly result: CommandResult;
}

const SUCCESSFUL_COMMAND_EXIT_CODE = 0;
const PKG_CONFIG_BINARY_NAME = "pkg-config";
const SUCCESS_EXIT_CODE = 0;
const REQUIRED_FAILURE_EXIT_CODE = 1;
const VERSION_PART_STEP = 1;
const DEFAULT_VERSION_PART = 0;

const parseVersionParts = (version: string): readonly number[] =>
  version.split(".").map((part) => Math.trunc(Number(part)));

const isVersionAtLeast = (foundVersion: string, minimumVersion: string): boolean => {
  const foundParts = parseVersionParts(foundVersion);
  const minimumParts = parseVersionParts(minimumVersion);
  const partCount = Math.max(foundParts.length, minimumParts.length);

  for (let partIndex = 0; partIndex < partCount; partIndex += VERSION_PART_STEP) {
    const foundPart = foundParts[partIndex] ?? DEFAULT_VERSION_PART;
    const minimumPart = minimumParts[partIndex] ?? DEFAULT_VERSION_PART;

    if (foundPart !== minimumPart) {
      return foundPart > minimumPart;
    }
  }

  return true;
};

const findWorkingCandidate = (
  candidateNames: readonly string[],
  versionArguments: readonly string[],
  environment: ProbeEnvironment,
): FoundBinary | null => {
  for (const candidateName of candidateNames) {
    const result = environment.runCommand(candidateName, versionArguments);

    if (result !== null) {
      return { binaryName: candidateName, result };
    }
  }

  return null;
};

const resolveCandidateNames = (probe: BinaryProbe, environment: ProbeEnvironment): readonly string[] => {
  const overrideValue =
    typeof probe.overrideEnvironmentVariable === "string"
      ? environment.readEnvironmentVariable(probe.overrideEnvironmentVariable)
      : null;

  return overrideValue === null || overrideValue === "" ? probe.candidateNames : [overrideValue];
};

const evaluateFoundVersion = (probe: BinaryProbe, found: FoundBinary): ProbeEvaluation => {
  if (typeof probe.minimumVersion !== "string") {
    return { detail: `found ${found.binaryName}`, outcome: "pass" };
  }

  const foundVersion = probe.versionPattern.exec(found.result.output)?.groups?.["version"] ?? null;

  if (foundVersion === null) {
    return { detail: `found ${found.binaryName} but could not parse its version`, outcome: "fail" };
  }

  if (!isVersionAtLeast(foundVersion, probe.minimumVersion)) {
    return { detail: `found ${found.binaryName} ${foundVersion}, need >= ${probe.minimumVersion}`, outcome: "fail" };
  }

  return { detail: `found ${found.binaryName} ${foundVersion}`, outcome: "pass" };
};

const evaluateBinaryProbe = (probe: BinaryProbe, environment: ProbeEnvironment): ProbeEvaluation => {
  const candidateNames = resolveCandidateNames(probe, environment);
  const found = findWorkingCandidate(candidateNames, probe.versionArguments, environment);

  if (found === null) {
    return { detail: `"${candidateNames.join('" or "')}" was not found on PATH`, outcome: "fail" };
  }

  return evaluateFoundVersion(probe, found);
};

const evaluatePkgConfigProbe = (probe: PkgConfigProbe, environment: ProbeEnvironment): ProbeEvaluation => {
  const result = environment.runCommand(PKG_CONFIG_BINARY_NAME, ["--exists", probe.moduleName]);

  if (result !== null && result.exitCode === SUCCESSFUL_COMMAND_EXIT_CODE) {
    return { detail: `pkg-config found "${probe.moduleName}"`, outcome: "pass" };
  }

  return { detail: `pkg-config could not find "${probe.moduleName}"`, outcome: "fail" };
};

const findMatchingDirectory = (probe: FileGlobProbe, environment: ProbeEnvironment): string | null => {
  for (const directoryPath of probe.searchDirectories) {
    if (environment.listDirectoryEntries(directoryPath).some((entryName) => probe.fileNamePattern.test(entryName))) {
      return directoryPath;
    }
  }

  return null;
};

const evaluateFileGlobProbe = (probe: FileGlobProbe, environment: ProbeEnvironment): ProbeEvaluation => {
  const matchingDirectory = findMatchingDirectory(probe, environment);

  if (matchingDirectory === null) {
    return {
      detail: `no file matching ${probe.fileNamePattern.source} under ${probe.searchDirectories.join(" or ")}`,
      outcome: "fail",
    };
  }

  return { detail: `found under ${matchingDirectory}`, outcome: "pass" };
};

const evaluateEnvVarProbe = (probe: EnvVarProbe, environment: ProbeEnvironment): ProbeEvaluation => {
  const value = environment.readEnvironmentVariable(probe.variableName);

  if (value === null || value === "") {
    return { detail: `${probe.variableName} is not set`, outcome: "fail" };
  }

  return { detail: `${probe.variableName}=${value}`, outcome: "pass" };
};

const evaluateLavapipeIcdProbe = (environment: ProbeEnvironment): ProbeEvaluation =>
  environment.hasLavapipeIcd()
    ? { detail: "found a lavapipe ICD manifest or libvulkan_lvp.so", outcome: "pass" }
    : { detail: "no lavapipe ICD manifest or libvulkan_lvp.so found", outcome: "fail" };

const evaluateProbe = (probe: Probe, environment: ProbeEnvironment): ProbeEvaluation => {
  switch (probe.kind) {
    case "binary": {
      return evaluateBinaryProbe(probe, environment);
    }
    case "env-var": {
      return evaluateEnvVarProbe(probe, environment);
    }
    case "file-glob": {
      return evaluateFileGlobProbe(probe, environment);
    }
    case "lavapipe-icd": {
      return evaluateLavapipeIcdProbe(environment);
    }
    case "pkg-config": {
      return evaluatePkgConfigProbe(probe, environment);
    }
    // No default
  }
};

const evaluateCheck = (check: DoctorCheck, environment: ProbeEnvironment): CheckResult => ({
  check,
  ...evaluateProbe(check.probe, environment),
});

const evaluateChecks = (
  checks: readonly DoctorCheck[],
  environment: ProbeEnvironment,
  selectedTiers: readonly Tier[],
): readonly CheckResult[] =>
  checks.filter((check) => selectedTiers.includes(check.tier)).map((check) => evaluateCheck(check, environment));

const computeExitCode = (results: readonly CheckResult[]): number =>
  results.some((result) => result.outcome === "fail" && result.check.required)
    ? REQUIRED_FAILURE_EXIT_CODE
    : SUCCESS_EXIT_CODE;

export { computeExitCode, evaluateCheck, evaluateChecks, isVersionAtLeast };
export type { CheckResult, CommandResult, ProbeEnvironment };
