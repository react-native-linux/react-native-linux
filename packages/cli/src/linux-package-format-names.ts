import type { LinuxBundleManifest } from "./linux-bundle-manifest.ts";

type HostArchitecture = "aarch64" | "x86_64";

type LinuxPackageFormat = "deb" | "pacman";

interface FormatArtifactNames {
  readonly packageName: string;
  readonly architectureToken: string;
  readonly artifactFilename: string;
}

interface FormatNameRules {
  readonly architectureTokens: Readonly<Record<HostArchitecture, string>>;
  readonly artifactFilename: (packageName: string, version: string, architectureToken: string) => string;
}

const packageNameSegmentSeparators = /[\s_]+/gu;
const camelCaseBoundary = /(?<lowerOrDigit>[a-z0-9])(?<upper>[A-Z])/gu;
const disallowedPackageNameCharacters = /[^a-z0-9-]/gu;
const characterAfterDot = 1;

const derivePackageName = (manifest: LinuxBundleManifest): string => {
  const { applicationIdentifier } = manifest;
  const lastIdentifierSegment = applicationIdentifier.slice(applicationIdentifier.lastIndexOf(".") + characterAfterDot);

  return lastIdentifierSegment
    .replaceAll(camelCaseBoundary, "$<lowerOrDigit>-$<upper>")
    .replaceAll(packageNameSegmentSeparators, "-")
    .toLowerCase()
    .replaceAll(disallowedPackageNameCharacters, "");
};

const pacmanPackageRelease = "1";

const formatNameRules: Record<LinuxPackageFormat, FormatNameRules> = {
  deb: {
    architectureTokens: { aarch64: "arm64", x86_64: "amd64" },
    artifactFilename: (packageName, version, architectureToken) => `${packageName}_${version}_${architectureToken}.deb`,
  },
  pacman: {
    architectureTokens: { aarch64: "aarch64", x86_64: "x86_64" },
    artifactFilename: (packageName, version, architectureToken) =>
      `${packageName}-${version}-${pacmanPackageRelease}-${architectureToken}.pkg.tar.zst`,
  },
};

const deriveFormatArtifactNames = (
  manifest: LinuxBundleManifest,
  format: LinuxPackageFormat,
  hostArchitecture: HostArchitecture,
): FormatArtifactNames => {
  const rules = formatNameRules[format];
  const packageName = derivePackageName(manifest);
  const architectureToken = rules.architectureTokens[hostArchitecture];

  return {
    architectureToken,
    artifactFilename: rules.artifactFilename(packageName, manifest.version, architectureToken),
    packageName,
  };
};

export type { FormatArtifactNames, HostArchitecture, LinuxPackageFormat };
export { deriveFormatArtifactNames, derivePackageName, formatNameRules };
