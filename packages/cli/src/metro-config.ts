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

export { linuxOverlayIndex, resolveLinuxOverlay };
