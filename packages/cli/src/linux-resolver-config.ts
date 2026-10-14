import { createLinuxResolveRequest } from "./metro-config.ts";
import { existsSync } from "node:fs";
import path from "node:path";

interface LinuxResolverConfig {
  readonly nodeModulesPaths: readonly string[];
  readonly platforms: readonly string[];
  readonly resolveRequest: ReturnType<typeof createLinuxResolveRequest>;
}

/**
 * The `resolver` section of a linux Metro config for the application at `projectRoot`. The overlays under
 * `@react-native-linux/core` import `react-native`, which only the application installs, so its `node_modules` is
 * the lookup path for them too.
 */
const linuxResolverConfig = (projectRoot: string): LinuxResolverConfig => {
  const nodeModulesDirectory = path.join(projectRoot, "node_modules");

  return {
    nodeModulesPaths: [nodeModulesDirectory],
    platforms: ["linux", "android", "ios"],
    resolveRequest: createLinuxResolveRequest((packageName) =>
      existsSync(path.join(nodeModulesDirectory, packageName, "package.json")),
    ),
  };
};

export { linuxResolverConfig };
