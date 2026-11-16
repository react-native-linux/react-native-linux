import { FileStore } from "metro-cache";
import { createRequire } from "node:module";
import { linuxResolverConfig } from "@react-native-linux/cli/linux-resolver-config.ts";
import type { loadConfig } from "metro";
import path from "node:path";

type MetroConfig = Awaited<ReturnType<typeof loadConfig>>;
type ResolveRequest = NonNullable<MetroConfig["resolver"]["resolveRequest"]>;

const linuxPlatform = "linux";
const repositoryRoot = path.resolve(import.meta.dirname, "..", "..", "..");
const profileDirectory = path.join(import.meta.dirname, "..", "test-bundles", "t3code-desktop-profile");
// The module system the bundling Metro prepends, which lives beside it rather than in the application.
const metroRuntimeDirectory = path.dirname(
  createRequire(createRequire(import.meta.url).resolve("metro")).resolve("metro-runtime/package.json"),
);

/**
 * The desktop feature profile of docs/research/t3code-linux.md §2: a module it leaves out or replaces that T3 Code
 * still imports resolves to the file here, which answers the way the profile says Linux does instead.
 */
const desktopProfile: Readonly<Record<string, string>> = {
  "@expo/ui/community/datetime-picker": path.join(profileDirectory, "expo-ui-datetime-picker.tsx"),
  "expo-audio": path.join(profileDirectory, "expo-audio.ts"),
  "expo-camera": path.join(profileDirectory, "expo-camera.ts"),
  "expo-keep-awake": path.join(profileDirectory, "expo-keep-awake.ts"),
  "expo-observe": path.join(profileDirectory, "expo-observe.ts"),
  "expo-sensors": path.join(profileDirectory, "expo-sensors.ts"),
  "expo-updates": path.join(profileDirectory, "expo-updates.ts"),
  "react-native-nitro-modules": path.join(profileDirectory, "react-native-nitro-modules.ts"),
};

/**
 * T3 Code's react-native-screens fork ships these three as `.ios`/`.android` pairs with no base file, so a linux
 * bundle cannot resolve them at all. The fixture takes the Android variant until #166 decides the screens shim.
 */
const screensPairsWithoutBase = new Set(["./StackHeaderConfig", "./TabsHost", "./TabsScreen"]);
const screensPackageSegment = `${path.sep}react-native-screens${path.sep}`;

const androidVariantSpecifier = (moduleName: string, originModulePath: string): string | null =>
  screensPairsWithoutBase.has(moduleName) && originModulePath.includes(screensPackageSegment)
    ? `${moduleName}.android`
    : null;

/** This platform's resolver chain ahead of the application's own resolver, uniwind's in T3 Code. */
const createResolveRequest = (mobileRoot: string, appResolveRequest: ResolveRequest | null): ResolveRequest => {
  const linux = linuxResolverConfig(mobileRoot);

  return (context, moduleName, platform) => {
    const profileFile = platform === linuxPlatform ? (desktopProfile[moduleName] ?? null) : null;

    if (profileFile !== null) {
      return { filePath: profileFile, type: "sourceFile" };
    }

    const throughApplication: typeof context = {
      ...context,
      resolveRequest:
        appResolveRequest === null
          ? context.resolveRequest
          : (innerContext, name, innerPlatform) =>
              appResolveRequest({ ...innerContext, resolveRequest: context.resolveRequest }, name, innerPlatform),
    };
    const variant = androidVariantSpecifier(moduleName, context.originModulePath);

    try {
      return linux.resolveRequest(throughApplication, moduleName, platform);
    } catch (error) {
      if (variant === null) {
        throw error;
      }

      return linux.resolveRequest(throughApplication, variant, platform);
    }
  };
};

/**
 * T3 Code's own Metro config, Expo's with uniwind's resolver, with the linux layer on top: the resolver above, and
 * the `react-native` export condition, which Expo's config grants per platform and without which a package such as
 * uniwind resolves its web build.
 */
const withLinuxLayer = (appConfig: MetroConfig, mobileRoot: string): MetroConfig => ({
  ...appConfig,
  // Off /tmp, as the harness app's own config keeps it.
  cacheStores: [new FileStore({ root: path.join(repositoryRoot, "build", "metro-cache") })],
  resolver: {
    ...appConfig.resolver,
    nodeModulesPaths: [path.join(mobileRoot, "node_modules"), ...appConfig.resolver.nodeModulesPaths],
    platforms: [...new Set([...appConfig.resolver.platforms, linuxPlatform])],
    resolveRequest: createResolveRequest(mobileRoot, appConfig.resolver.resolveRequest ?? null),
    unstable_conditionsByPlatform: { ...appConfig.resolver.unstable_conditionsByPlatform, linux: ["react-native"] },
  },
  watchFolders: [
    ...appConfig.watchFolders,
    path.join(repositoryRoot, "packages", "core"),
    profileDirectory,
    metroRuntimeDirectory,
  ],
});

export { withLinuxLayer };
