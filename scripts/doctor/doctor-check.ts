type Tier = "coverage" | "core" | "goldens" | "window";

interface DistroRemedy {
  readonly arch: string;
  readonly ubuntu: string;
}

interface BinaryProbe {
  readonly kind: "binary";
  readonly candidateNames: readonly string[];
  readonly versionArguments: readonly string[];
  readonly versionPattern: RegExp;
  readonly minimumVersion?: string;
  readonly overrideEnvironmentVariable?: string;
}

interface PkgConfigProbe {
  readonly kind: "pkg-config";
  readonly moduleName: string;
}

interface FileGlobProbe {
  readonly kind: "file-glob";
  readonly searchDirectories: readonly string[];
  readonly fileNamePattern: RegExp;
}

interface EnvVarProbe {
  readonly kind: "env-var";
  readonly variableName: string;
}

interface LavapipeIcdProbe {
  readonly kind: "lavapipe-icd";
}

type Probe = BinaryProbe | EnvVarProbe | FileGlobProbe | LavapipeIcdProbe | PkgConfigProbe;

interface DoctorCheck {
  readonly name: string;
  readonly tier: Tier;
  readonly required: boolean;
  readonly why: string;
  readonly probe: Probe;
  readonly remedy: DistroRemedy;
}

export type { BinaryProbe, DistroRemedy, DoctorCheck, EnvVarProbe, FileGlobProbe, PkgConfigProbe, Probe, Tier };
