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
 * The variant a linux bundle takes of a resolved package file that `linuxPackageVariantIndex` names, the package
 * counterpart of `react-native`'s `linuxUpstreamVariantIndex`; null for every other file.
 */
const resolveLinuxPackageVariantForResolvedFile = (filePath: string, platform: string | null): string | null => {
  const packageSubpath = packageSubpathPattern.exec(filePath)?.groups?.["subpath"] ?? "";
  const variant = linuxPackageVariantIndex[packageSubpath] ?? null;

  return platform !== linuxPlatform || variant === null
    ? null
    : filePath.replace(sourceFileExtensionPattern, `.${variant}$&`);
};

export { resolveLinuxPackageVariantForResolvedFile };
