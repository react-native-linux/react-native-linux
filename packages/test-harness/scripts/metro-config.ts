import { getDefaultConfig, mergeConfig } from "@react-native/metro-config";
import { FileStore } from "metro-cache";
import { createLinuxResolveRequest } from "@react-native-linux/cli/metro-config.ts";
import { existsSync } from "node:fs";
import path from "node:path";

const projectRoot = path.resolve(import.meta.dirname, "..");

const repositoryRoot = path.resolve(projectRoot, "..", "..");

const isPackageResolvable = (packageName: string): boolean =>
  existsSync(path.join(projectRoot, "node_modules", packageName, "package.json"));

const harnessMetroConfig = mergeConfig(getDefaultConfig(projectRoot), {
  // Off /tmp, whose per-user quota this machine's other work shares.
  cacheStores: [new FileStore({ root: path.join(repositoryRoot, "build", "metro-cache") })],
  resolver: {
    // The overlays under packages/core import react-native, which only this package installs.
    nodeModulesPaths: [path.join(projectRoot, "node_modules")],
    platforms: ["linux", "android", "ios"],
    resolveRequest: createLinuxResolveRequest(isPackageResolvable),
  },
  watchFolders: [repositoryRoot],
});

export { harnessMetroConfig, repositoryRoot };
