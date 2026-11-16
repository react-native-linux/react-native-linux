import { getDefaultConfig, mergeConfig } from "@react-native/metro-config";
import { FileStore } from "metro-cache";
import { babelCacheVersion } from "@react-native-linux/cli/babel-cache-version.ts";
import { linuxResolverConfig } from "@react-native-linux/cli/linux-resolver-config.ts";
import path from "node:path";

const projectRoot = path.resolve(import.meta.dirname, "..");

const repositoryRoot = path.resolve(projectRoot, "..", "..");

const harnessMetroConfig = mergeConfig(getDefaultConfig(projectRoot), {
  // Off /tmp, whose per-user quota this machine's other work shares.
  cacheStores: [new FileStore({ root: path.join(repositoryRoot, "build", "metro-cache") })],
  cacheVersion: babelCacheVersion(projectRoot),
  resolver: {
    ...linuxResolverConfig(projectRoot),
    // The t3-expo-ui bundle's stand-in for the T3 Code package its CustomSnoozeSheet variant imports.
    extraNodeModules: { "@t3tools/client-runtime": path.join(projectRoot, "modules", "t3-expo-ui", "client-runtime") },
  },
  watchFolders: [repositoryRoot],
});

export { harnessMetroConfig, repositoryRoot };
