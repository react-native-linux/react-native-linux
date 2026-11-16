import { getDefaultConfig, mergeConfig } from "@react-native/metro-config";
import { FileStore } from "metro-cache";
import { babelCacheVersion } from "@react-native-linux/cli/babel-cache-version.ts";
import { linuxResolverConfig } from "@react-native-linux/cli/linux-resolver-config.ts";
import path from "node:path";

const projectRoot = path.resolve(import.meta.dirname, "..");

const repositoryRoot = path.resolve(projectRoot, "..", "..");

const linuxResolver = linuxResolverConfig(projectRoot);
type ResolveRequest = typeof linuxResolver.resolveRequest;

/**
 * The ESM build of gt-i18n reads `import.meta.env`, which Hermes cannot parse on any platform. Expo's Babel preset
 * rewrites `import.meta` and React Native's does not, so the harness takes gt-i18n's CommonJS build, which reads
 * `({}).env`, what that rewrite leaves.
 */
const resolveRequest: ResolveRequest = (context, moduleName, platform) =>
  linuxResolver.resolveRequest(
    moduleName === "gt-i18n" || moduleName.startsWith("gt-i18n/") ? { ...context, isESMImport: false } : context,
    moduleName,
    platform,
  );

const harnessMetroConfig = mergeConfig(getDefaultConfig(projectRoot), {
  // Off /tmp, whose per-user quota this machine's other work shares.
  cacheStores: [new FileStore({ root: path.join(repositoryRoot, "build", "metro-cache") })],
  cacheVersion: babelCacheVersion(projectRoot),
  resolver: {
    ...linuxResolver,
    extraNodeModules: {
      // The t3-expo-ui bundle's stand-in for the T3 Code package its CustomSnoozeSheet variant imports.
      "@t3tools/client-runtime": path.join(projectRoot, "modules", "t3-expo-ui", "client-runtime"),
      // T3 Code's own dependency, which the t3-file-preview bundle's FilePreviewModal variant copies local files with.
      "expo-file-system": path.join(projectRoot, "..", "expo", "node_modules", "expo-file-system"),
    },
    resolveRequest,
  },
  watchFolders: [repositoryRoot],
});

export { harnessMetroConfig, repositoryRoot };
