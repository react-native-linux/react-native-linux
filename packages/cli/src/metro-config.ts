import { packageAliases, resolveLinuxPackageAlias } from "./package-aliases.ts";
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

const coreLinuxNativeComponentDirectory = path.join(coreLinuxUtilitiesDirectory, "..", "NativeComponent");

const linuxOverlayIndex: Readonly<Record<string, string>> = {
  "Libraries/NativeComponent/BaseViewConfig": path.join(coreLinuxNativeComponentDirectory, "BaseViewConfig.linux.ts"),
  "Libraries/StyleSheet/PlatformColorValueTypes": path.join(
    coreLinuxUtilitiesDirectory,
    "..",
    "StyleSheet",
    "PlatformColorValueTypes.linux.ts",
  ),
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

/**
 * The name-rewriting head of the chain: the fixed `react-native` subpath overrides first, then the overlay
 * packages of issue #180. Returns null when neither applies, which leaves the module name to the package-name
 * redirect and the extension chain below.
 */
const resolveLinuxModuleName = (
  moduleName: string,
  platform: string | null,
  isPackageResolvable: (packageName: string) => boolean,
): string | null =>
  resolveLinuxOverlay(moduleName, platform, linuxOverlayIndex) ??
  resolveLinuxPackageAlias({ aliases: packageAliases, isPackageResolvable, moduleName, platform });

/**
 * The upstream files that exist only as `.android`/`.ios` pairs behind a base file re-exporting its own
 * platform-specific self, which a `linux` resolution would otherwise resolve back to the base and import as
 * `undefined` (#22). Until one of them needs behaviour of its own and gets an entry in `linuxOverlayIndex`, a linux
 * bundle takes the upstream variant named here.
 */
const linuxUpstreamVariantIndex: Readonly<Record<string, "android" | "ios">> = {
  // NativeAlertManager's alert, the desktop dialog shape; Android's is DialogManagerAndroid.
  "Libraries/Alert/RCTAlertManager": "ios",
  // The Accessibility manager's focus call; Android's goes through UIManager commands this platform does not serve.
  "Libraries/Components/AccessibilityInfo/legacySendAccessibilityEvent": "ios",
  // A stub that warns; Android's needs the Android drawer view.
  "Libraries/Components/DrawerAndroid/DrawerLayoutAndroid": "ios",
  // The only variant upstream ships; it needs AndroidProgressBar, and only when rendered.
  "Libraries/Components/ProgressBarAndroid/ProgressBarAndroid": "android",
  // A stub that warns; Android's needs NativeToastAndroid.
  "Libraries/Components/ToastAndroid/ToastAndroid": "ios",
  // Android's image source handling: `loadingIndicatorSource` and `resizeMethod`, which this renderer reads.
  "Libraries/Image/Image": "android",
  // NativeNetworkingAndroid is the spec of the C++ Networking module this platform registers (#79).
  "Libraries/Network/RCTNetworking": "android",
  // The only variant upstream ships.
  "Libraries/Settings/Settings": "ios",
  // A desktop has no hardware back button, and the iOS variant is the one without one.
  "Libraries/Utilities/BackHandler": "ios",
};

const reactNativePackageSegment = `${path.sep}react-native${path.sep}`;
const sourceFileExtensionPattern = /\.(?:js|jsx|ts|tsx)$/u;

/**
 * #22: React Native's own modules reach an overridden file relatively — `Libraries/Utilities/Platform.js` is
 * `import Platform from './Platform'`, written for a resolver that picks `Platform.<platform>.js` — so rewriting
 * the specifier an application imports is not enough. The overlay replaces the upstream file whatever specifier
 * resolved to it.
 */
const resolveLinuxOverlayForResolvedFile = (filePath: string, platform: string | null): string | null => {
  if (platform !== linuxPlatform || !filePath.includes(reactNativePackageSegment)) {
    return null;
  }

  const segmentIndex = filePath.lastIndexOf(reactNativePackageSegment);

  const upstreamSubpath = filePath
    .slice(segmentIndex + reactNativePackageSegment.length)
    .replace(sourceFileExtensionPattern, "");
  const upstreamVariant = linuxUpstreamVariantIndex[upstreamSubpath] ?? null;

  return (
    linuxOverlayIndex[upstreamSubpath] ??
    (upstreamVariant === null ? null : filePath.replace(sourceFileExtensionPattern, `.${upstreamVariant}$&`))
  );
};

interface SourceFileResolution {
  readonly filePath: string;
  readonly type: "sourceFile";
}

interface LinuxResolutionContext<Resolution> {
  readonly resolveRequest: (
    context: LinuxResolutionContext<Resolution>,
    moduleName: string,
    platform: string | null,
  ) => Resolution | SourceFileResolution;
}

/**
 * The Metro `resolveRequest` of a linux bundle: the specifier rewrites of `resolveLinuxModuleName` first, Metro's
 * own resolution — whose `linux`, `native`, default extension chain is `resolvePlatformCandidates` — next, and the
 * overlay substitution of `resolveLinuxOverlayForResolvedFile` on whatever file that produced.
 */
const createLinuxResolveRequest =
  (isPackageResolvable: (packageName: string) => boolean) =>
  <Resolution>(
    context: LinuxResolutionContext<Resolution>,
    moduleName: string,
    platform: string | null,
  ): Resolution | SourceFileResolution => {
    const resolution = context.resolveRequest(
      context,
      resolveLinuxModuleName(moduleName, platform, isPackageResolvable) ?? moduleName,
      platform,
    );
    const overlayPath =
      typeof resolution === "object" &&
      resolution !== null &&
      "filePath" in resolution &&
      typeof resolution.filePath === "string"
        ? resolveLinuxOverlayForResolvedFile(resolution.filePath, platform)
        : null;

    return overlayPath === null ? resolution : { filePath: overlayPath, type: "sourceFile" };
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
  createLinuxResolveRequest,
  linuxOverlayIndex,
  linuxUpstreamVariantIndex,
  resolveAgainstFilesystem,
  resolveLinuxModuleName,
  resolveLinuxOverlay,
  resolveLinuxOverlayForResolvedFile,
  resolveOriginAwareCandidates,
  resolvePlatformCandidates,
  shouldUseJavaScriptFallback,
};
