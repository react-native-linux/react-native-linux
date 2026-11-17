import { coreDirectory, resolveLinuxPackageOverlayForResolvedFile } from "./package-overlays.ts";
import { packageAliases, resolveLinuxPackageAlias } from "./package-aliases.ts";
import path from "node:path";

const reactNativeModulePrefix = "react-native/";
const linuxPlatform = "linux";

const coreLinuxUtilitiesDirectory = path.join(coreDirectory, "src-linux", "Libraries", "Utilities");

const coreLinuxNativeComponentDirectory = path.join(coreLinuxUtilitiesDirectory, "..", "NativeComponent");

/**
 * The overlays with no upstream file to replace: React Native's patched modules require them by name, so they are
 * reached only by rewriting that specifier. Every other overlay replaces the upstream file a specifier resolves to.
 */
const linuxOnlyOverlayIndex: Readonly<Record<string, string>> = {
  "Libraries/Components/TextInput/LinuxTextInputNativeComponent": path.join(
    coreLinuxUtilitiesDirectory,
    "..",
    "Components",
    "TextInput",
    "LinuxTextInputNativeComponent.linux.ts",
  ),
};

const linuxOverlayIndex: Readonly<Record<string, string>> = {
  ...linuxOnlyOverlayIndex,
  "Libraries/Alert/Alert": path.join(coreLinuxUtilitiesDirectory, "..", "Alert", "Alert.linux.ts"),
  "Libraries/Alert/RCTAlertManager": path.join(coreLinuxUtilitiesDirectory, "..", "Alert", "RCTAlertManager.linux.ts"),
  "Libraries/NativeComponent/BaseViewConfig": path.join(coreLinuxNativeComponentDirectory, "BaseViewConfig.linux.ts"),
  "Libraries/ReactNative/AppContainer": path.join(
    coreLinuxUtilitiesDirectory,
    "..",
    "ReactNative",
    "AppContainer.linux.tsx",
  ),
  "Libraries/StyleSheet/PlatformColorValueTypes": path.join(
    coreLinuxUtilitiesDirectory,
    "..",
    "StyleSheet",
    "PlatformColorValueTypes.linux.ts",
  ),
  "Libraries/StyleSheet/processColor": path.join(
    coreLinuxUtilitiesDirectory,
    "..",
    "StyleSheet",
    "processColor.linux.ts",
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
 * The name-rewriting head of the chain: the `react-native` subpaths of `linuxOnlyOverlayIndex` first, then the overlay
 * packages of issue #180. Returns null when neither applies, which leaves the module name to the package-name
 * redirect and the extension chain below.
 */
const resolveLinuxModuleName = (
  moduleName: string,
  platform: string | null,
  isPackageResolvable: (packageName: string) => boolean,
): string | null =>
  resolveLinuxOverlay(moduleName, platform, linuxOnlyOverlayIndex) ??
  resolveLinuxPackageAlias({ aliases: packageAliases, isPackageResolvable, moduleName, platform });

/**
 * The upstream files that exist only as `.android`/`.ios` pairs behind a base file re-exporting its own
 * platform-specific self, which a `linux` resolution would otherwise resolve back to the base and import as
 * `undefined` (#22). Until one of them needs behaviour of its own and gets an entry in `linuxOverlayIndex`, a linux
 * bundle takes the upstream variant named here.
 */
const linuxUpstreamVariantIndex: Readonly<Record<string, "android" | "ios">> = {
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
  // Android's emits `hardwareBackPress`, which an unconsumed Escape raises (#788).
  // Its `exitApp` does nothing without a `DeviceEventManager` module, as in react-native-windows, which ships it too.
  "Libraries/Utilities/BackHandler": "android",
  // No base file at all, only the pair, and a development bundle requires it. Android's reads an optional module.
  // The iOS one reaches NativeSettingsManager through getEnforcing, which throws on a platform that does not serve it.
  "src/private/devsupport/rndevtools/ReactDevToolsSettingsManager": "android",
};

const NOT_FOUND = -1;
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
  readonly originModulePath?: string;
  readonly resolveRequest: (
    context: LinuxResolutionContext<Resolution>,
    moduleName: string,
    platform: string | null,
  ) => Resolution | SourceFileResolution;
}

/**
 * An upstream pair with no base file cannot resolve at all for `linux`, so the substitution above never sees it: a
 * relative specifier from inside `react-native` that names one is retried as its variant, `<specifier>.<variant>`.
 */
const baselessVariantSpecifier = (
  moduleName: string,
  originModulePath: string | null,
  platform: string | null,
): string | null => {
  if (platform !== linuxPlatform || originModulePath === null || !moduleName.startsWith(".")) {
    return null;
  }

  const target = path.resolve(path.dirname(originModulePath), moduleName);
  const segmentIndex = target.lastIndexOf(reactNativePackageSegment);
  const variant =
    segmentIndex === NOT_FOUND
      ? null
      : (linuxUpstreamVariantIndex[target.slice(segmentIndex + reactNativePackageSegment.length)] ?? null);

  return variant === null ? null : `${moduleName}.${variant}`;
};

const resolveWithBaselessVariant = <Resolution>(
  resolve: (specifier: string) => Resolution,
  specifier: string,
  variantSpecifier: string | null,
): Resolution => {
  try {
    return resolve(specifier);
  } catch (error) {
    if (variantSpecifier === null) {
      throw error;
    }

    return resolve(variantSpecifier);
  }
};

/**
 * The Metro `resolveRequest` of a linux bundle: the specifier rewrites of `resolveLinuxModuleName` first, Metro's
 * own resolution — whose `linux`, `native`, default extension chain is `resolvePlatformCandidates` — next, and the
 * overlay substitution of `resolveLinuxOverlayForResolvedFile`, or else the package overlay of
 * `resolveLinuxPackageOverlayForResolvedFile`, on whatever file that produced. An overlay lives in
 * `@react-native-linux/core`, which can sit outside the application's tree and watch folders, as it does for a
 * checkout bundled against this repository; resolved from there, its package imports reach core's own installs, such as
 * its `@react-native/normalize-colors`, rather than the application's. So they resolve from the file it replaced, as
 * that file's would, and its relative imports stay its own files.
 */
const createLinuxResolveRequest = (isPackageResolvable: (packageName: string) => boolean) => {
  const replacedFileByOverlay = new Map<string, string>();
  const takeOverlay = (resolvedFile: string, platform: string | null): string | null => {
    const overlayPath =
      resolveLinuxOverlayForResolvedFile(resolvedFile, platform) ??
      resolveLinuxPackageOverlayForResolvedFile(resolvedFile, platform);

    if (overlayPath !== null) {
      replacedFileByOverlay.set(overlayPath, resolvedFile);
    }

    return overlayPath;
  };

  return <Resolution>(
    context: LinuxResolutionContext<Resolution>,
    moduleName: string,
    platform: string | null,
  ): Resolution | SourceFileResolution => {
    const replacedFile = moduleName.startsWith(".")
      ? null
      : (replacedFileByOverlay.get(context.originModulePath ?? "") ?? null);
    const originContext = replacedFile === null ? context : { ...context, originModulePath: replacedFile };
    const resolution = resolveWithBaselessVariant(
      (specifier) => context.resolveRequest(originContext, specifier, platform),
      resolveLinuxModuleName(moduleName, platform, isPackageResolvable) ?? moduleName,
      baselessVariantSpecifier(moduleName, context.originModulePath ?? null, platform),
    );
    const resolvedFile =
      typeof resolution === "object" &&
      resolution !== null &&
      "filePath" in resolution &&
      typeof resolution.filePath === "string"
        ? resolution.filePath
        : null;
    const overlayPath = resolvedFile === null ? null : takeOverlay(resolvedFile, platform);

    return overlayPath === null ? resolution : { filePath: overlayPath, type: "sourceFile" };
  };
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

export {
  createLinuxResolveRequest,
  linuxOverlayIndex,
  linuxUpstreamVariantIndex,
  resolveAgainstFilesystem,
  resolveLinuxModuleName,
  resolveLinuxOverlay,
  resolveLinuxOverlayForResolvedFile,
  resolvePlatformCandidates,
};
