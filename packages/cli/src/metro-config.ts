import path from "node:path";

const reactNativeModulePrefix = "react-native/";
const linuxPlatform = "linux";

const coreLinuxUtilitiesDirectory = path.join(
  import.meta.dirname,
  "..",
  "..",
  "core",
  "src-linux",
  "Libraries",
  "Utilities",
);

const linuxOverlayIndex: Readonly<Record<string, string>> = {
  "Libraries/Utilities/Platform": path.join(coreLinuxUtilitiesDirectory, "Platform.linux.ts"),
  "Libraries/Utilities/PlatformTypes": path.join(coreLinuxUtilitiesDirectory, "PlatformTypes.ts"),
};

const resolveLinuxOverlay = (
  moduleName: string,
  platform: string | null,
  overlayIndex: Readonly<Record<string, string>>,
): string | null => {
  if (platform !== linuxPlatform || !moduleName.startsWith(reactNativeModulePrefix)) {
    return null;
  }

  const upstreamSubpath = moduleName.slice(reactNativeModulePrefix.length);

  return overlayIndex[upstreamSubpath] ?? null;
};

const nativePlatformExtension = "native";

const buildPlatformExtensionCandidates = (
  moduleName: string,
  platform: string,
  sourceExtension: string,
): readonly string[] => [
  `${moduleName}.${platform}.${sourceExtension}`,
  `${moduleName}.${nativePlatformExtension}.${sourceExtension}`,
  `${moduleName}.${sourceExtension}`,
];

const resolvePlatformCandidates = (
  moduleName: string,
  platform: string,
  sourceExts: readonly string[],
): readonly string[] =>
  sourceExts.flatMap((sourceExtension) => buildPlatformExtensionCandidates(moduleName, platform, sourceExtension));

const resolveAgainstFilesystem = (
  candidates: readonly string[],
  exists: (candidatePath: string) => boolean,
): string | null => candidates.find((candidatePath) => exists(candidatePath)) ?? null;

const javaScriptFallbackPackageNames: readonly string[] = ["react-native-reanimated", "react-native-worklets"];

const relativeModulePrefix = ".";

interface LinuxResolutionRequest {
  readonly moduleName: string;
  readonly originModulePath: string;
  readonly platform: string;
}

const isInsideJavaScriptFallbackPackage = (originModulePath: string): boolean =>
  originModulePath.split(path.sep).some((pathSegment) => javaScriptFallbackPackageNames.includes(pathSegment));

const shouldUseJavaScriptFallback = (request: LinuxResolutionRequest): boolean =>
  request.platform === linuxPlatform &&
  request.moduleName.startsWith(relativeModulePrefix) &&
  isInsideJavaScriptFallbackPackage(request.originModulePath);

const buildJavaScriptFallbackCandidates = (
  moduleName: string,
  platform: string,
  sourceExts: readonly string[],
): readonly string[] =>
  sourceExts.flatMap((sourceExtension) => [
    `${moduleName}.${platform}.${sourceExtension}`,
    `${moduleName}.${sourceExtension}`,
  ]);

const resolveCandidateBasePath = (request: LinuxResolutionRequest): string =>
  request.moduleName.startsWith(relativeModulePrefix)
    ? path.join(path.dirname(request.originModulePath), request.moduleName)
    : request.moduleName;

const resolveOriginAwareCandidates = (
  request: LinuxResolutionRequest,
  sourceExts: readonly string[],
): readonly string[] => {
  const candidateBasePath = resolveCandidateBasePath(request);

  return shouldUseJavaScriptFallback(request)
    ? buildJavaScriptFallbackCandidates(candidateBasePath, request.platform, sourceExts)
    : resolvePlatformCandidates(candidateBasePath, request.platform, sourceExts);
};

export {
  linuxOverlayIndex,
  resolveAgainstFilesystem,
  resolveLinuxOverlay,
  resolveOriginAwareCandidates,
  resolvePlatformCandidates,
  shouldUseJavaScriptFallback,
};
