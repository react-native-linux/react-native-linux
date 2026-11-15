import path from "node:path";

const linuxPlatform = "linux";
const sourceFileExtensionPattern = /\.(?:js|jsx|ts|tsx)$/u;
const packageSubpathPattern = /.*node_modules[/\\](?<subpath>.+)\.(?:js|jsx|ts|tsx)$/u;

/**
 * ADR-0004: expo-modules-core installs its JavaScript `globalThis.expo`, the object model its web build and jest-expo
 * run on, only from the web variant of its polyfill, and its other modules read that global as they load. A linux
 * bundle takes that variant, so Expo's own JavaScript runs unchanged over this platform's TurboModules.
 */
const linuxPackageVariantIndex: Readonly<Record<string, "web">> = {
  "expo-modules-core/build/polyfill/index": "web",
};

/**
 * ADR-0004's escape hatch: a resolved package file whose JavaScript has to differ on Linux, and the specifier of the
 * linux package a linux bundle resolves instead. `expo`'s `fetch`, which `Expo.fx` installs as the global one, runs
 * over the native `ExpoFetchModule`, whose request and response are native classes upstream's JavaScript extends,
 * the constructor-backed shape #663 has yet to prove; until #673, both keep React Native's own `fetch`.
 */
const linuxPackageSubstituteIndex: Readonly<Record<string, string>> = {
  "expo/build/winter/fetch/index": "@react-native-linux/expo/fetch",
};

const packageSubpathOf = (filePath: string): string => packageSubpathPattern.exec(filePath)?.groups?.["subpath"] ?? "";

/**
 * The variant a linux bundle takes of a resolved package file that `linuxPackageVariantIndex` names, the package
 * counterpart of `react-native`'s `linuxUpstreamVariantIndex`; null for every other file.
 */
const resolveLinuxPackageVariantForResolvedFile = (filePath: string, platform: string | null): string | null => {
  const variant = linuxPackageVariantIndex[packageSubpathOf(filePath)] ?? null;

  return platform !== linuxPlatform || variant === null
    ? null
    : filePath.replace(sourceFileExtensionPattern, `.${variant}$&`);
};

/**
 * The specifier a linux bundle resolves instead of a resolved package file that `linuxPackageSubstituteIndex` names,
 * once the linux package that holds it is installed; null for every other file, as for a package alias whose linux
 * package is missing.
 */
const resolveLinuxPackageSubstituteForResolvedFile = (
  filePath: string,
  platform: string | null,
  isPackageResolvable: (packageName: string) => boolean,
): string | null => {
  const substitute = linuxPackageSubstituteIndex[packageSubpathOf(filePath)] ?? null;

  return platform !== linuxPlatform || substitute === null || !isPackageResolvable(path.posix.dirname(substitute))
    ? null
    : substitute;
};

export { resolveLinuxPackageSubstituteForResolvedFile, resolveLinuxPackageVariantForResolvedFile };
