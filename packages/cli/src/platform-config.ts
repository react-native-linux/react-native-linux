import { existsSync } from "node:fs";
import path from "node:path";

const defaultSourceDirectoryName = "linux";

interface LinuxPlatformParams {
  sourceDir?: string;
}

interface LinuxPlatformConfig {
  sourceDir: string;
}

interface OutOfTreePlatformRegistration {
  platforms: {
    linux: {
      npmPackageName: string;
      projectConfig: (root: string, params: LinuxPlatformParams) => LinuxPlatformConfig | null;
      dependencyConfig: (root: string, params: LinuxPlatformParams) => LinuxPlatformConfig | null;
    };
  };
}

const resolveLinuxSourceDirectory = (root: string, params: LinuxPlatformParams): LinuxPlatformConfig | null => {
  const sourceDir = path.join(root, params.sourceDir ?? defaultSourceDirectoryName);

  return existsSync(sourceDir) ? { sourceDir } : null;
};

export const platformConfig: OutOfTreePlatformRegistration = {
  platforms: {
    linux: {
      dependencyConfig: resolveLinuxSourceDirectory,
      npmPackageName: "@react-native-linux/core",
      projectConfig: resolveLinuxSourceDirectory,
    },
  },
};
