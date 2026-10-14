import { getDefaultConfig, mergeConfig } from "@react-native/metro-config";
import { FileStore } from "metro-cache";
import { linuxResolverConfig } from "@react-native-linux/cli/linux-resolver-config.ts";
import path from "node:path";

const projectRoot = path.resolve(import.meta.dirname, "..");

const repositoryRoot = path.resolve(projectRoot, "..", "..");

const harnessMetroConfig = mergeConfig(getDefaultConfig(projectRoot), {
  // Off /tmp, whose per-user quota this machine's other work shares.
  cacheStores: [new FileStore({ root: path.join(repositoryRoot, "build", "metro-cache") })],
  resolver: linuxResolverConfig(projectRoot),
  watchFolders: [repositoryRoot],
});

export { harnessMetroConfig, repositoryRoot };
