import { copyFileSync, existsSync, mkdirSync, readFileSync, realpathSync, rmSync } from "node:fs";
import { createRequire } from "node:module";
import { execPath } from "node:process";
import path from "node:path";
import { spawnSync } from "node:child_process";

const SUCCESSFUL_EXIT_STATUS = 0;
const PLUGIN_NAME_INDEX = 0;
const PLUGIN_OPTIONS_INDEX = 1;
const NO_FONTS = 0;

const isRecord = (value: unknown): value is Record<string, unknown> =>
  typeof value === "object" && value !== null && !Array.isArray(value);

const readStrings = (value: unknown): readonly string[] =>
  Array.isArray(value) ? value.filter((entry: unknown): entry is string => typeof entry === "string") : [];

/** The CommonJS function `name` that the untyped module at `modulePath` exports. */
const requireFunction = (modulePath: string, name: string): ((...parameters: readonly unknown[]) => unknown) => {
  const exports: unknown = createRequire(import.meta.url)(modulePath);
  const exported = isRecord(exports) ? exports[name] : null;

  if (typeof exported !== "function") {
    throw new TypeError(`${modulePath} exports no function ${name}`);
  }

  return (...parameters) => exported(...parameters);
};

/**
 * The font files `expo-font`'s config plugin embeds on iOS, `fonts` and `ios.fonts`: Linux resolves a face by its
 * family or PostScript name as iOS does, where Android's list names XML resource families instead.
 */
const declaredFonts = (appConfig: unknown): readonly string[] => {
  const plugins = isRecord(appConfig) ? appConfig["plugins"] : null;

  return (Array.isArray(plugins) ? plugins : []).flatMap((plugin: unknown) => {
    const options: unknown =
      Array.isArray(plugin) && plugin[PLUGIN_NAME_INDEX] === "expo-font" ? plugin[PLUGIN_OPTIONS_INDEX] : null;

    if (!isRecord(options)) {
      return [];
    }

    const { ios } = options;

    return [...readStrings(options["fonts"]), ...readStrings(isRecord(ios) ? ios["fonts"] : null)];
  });
};

/**
 * The package installed in a `node_modules` at or above `directory`, where expo-modules-autolinking looks for it: not
 * through `require.resolve`, which also searches `NODE_PATH`, and pnpm puts its whole store there for every script.
 */
const findInstalledPackage = (directory: string, packageName: string): string | null => {
  const candidate = path.join(directory, "node_modules", packageName);

  if (existsSync(path.join(candidate, "package.json"))) {
    return candidate;
  }

  return path.dirname(directory) === directory ? null : findInstalledPackage(path.dirname(directory), packageName);
};

/** The package `expo` itself depends on, found from expo's real directory as autolinking follows the dependency graph. */
const resolveExpoDependency = (expoDirectory: string, packageName: string): string => {
  const directory = findInstalledPackage(expoDirectory, packageName);

  if (directory === null) {
    throw new Error(`${expoDirectory} has no ${packageName} installed`);
  }

  return directory;
};

/**
 * The declared fonts and the project's own `assets/fonts`, as `expo-font`'s `resolveFontPaths` resolves them: a file, a
 * directory or a module.
 */
const resolveFonts = async (
  expoDirectory: string,
  projectRoot: string,
  declared: readonly string[],
): Promise<readonly string[]> => {
  const projectFonts = path.join(projectRoot, "assets", "fonts");
  const fonts = existsSync(projectFonts) ? [...declared, projectFonts] : declared;

  if (fonts.length === NO_FONTS) {
    return [];
  }

  const utilities = path.join(resolveExpoDependency(expoDirectory, "expo-font"), "plugin", "build", "utils.js");

  return readStrings(await requireFunction(utilities, "resolveFontPaths")(fonts, projectRoot));
};

/** Replaces `fontsDirectory` with copies of `fontPaths`, so a font an earlier build embedded does not linger. */
const embedFonts = (fontsDirectory: string, fontPaths: readonly string[]): void => {
  rmSync(fontsDirectory, { force: true, recursive: true });

  if (fontPaths.length === NO_FONTS) {
    return;
  }

  mkdirSync(fontsDirectory, { recursive: true });

  for (const fontPath of fontPaths) {
    copyFileSync(fontPath, path.join(fontsDirectory, path.basename(fontPath)));
  }
};

/** Runs upstream's `getAppConfig.js` into `assetsDirectory` and returns the config it wrote. */
const writeAppConfig = (constantsDirectory: string, projectRoot: string, assetsDirectory: string): unknown => {
  mkdirSync(assetsDirectory, { recursive: true });

  const script = path.join(constantsDirectory, "scripts", "getAppConfig.js");
  const evaluation = spawnSync(execPath, [script, projectRoot, assetsDirectory], {
    encoding: "utf8",
    stdio: ["ignore", "inherit", "pipe"],
  });

  if (evaluation.status !== SUCCESSFUL_EXIT_STATUS) {
    throw new Error(`expo-constants could not evaluate the app config of ${projectRoot}:\n${evaluation.stderr}`);
  }

  return JSON.parse(readFileSync(path.join(assetsDirectory, "app.config"), "utf8"));
};

/**
 * The linux build step for an Expo app (#696): the app config and fonts its native builds embed, written to
 * `assetsDirectory`, which `rnl_window` reads beside a bundle file or from `--assets` (ADR-0004). An app with `expo`
 * gets `app.config` from upstream's unmodified `expo-constants/scripts/getAppConfig.js`, the script Android's Gradle
 * step runs into the APK's assets, and in `fonts/` the files `expo-font`'s config plugin declares and the project's
 * `assets/fonts`. `expo` depends on both packages, so a project without it gets nothing, as on Android and iOS.
 */
const embedExpoAppAssets = async (projectDirectory: string, assetsDirectory: string): Promise<void> => {
  const projectRoot = path.resolve(projectDirectory);
  const destination = path.resolve(assetsDirectory);
  const installedExpo = findInstalledPackage(projectRoot, "expo");

  if (installedExpo === null) {
    return;
  }

  if (destination === path.join(projectRoot, "assets")) {
    throw new Error(`${destination} is the project's own assets directory, whose fonts the build would replace`);
  }

  const expoDirectory = realpathSync(installedExpo);
  const appConfig = writeAppConfig(resolveExpoDependency(expoDirectory, "expo-constants"), projectRoot, destination);

  embedFonts(path.join(destination, "fonts"), await resolveFonts(expoDirectory, projectRoot, declaredFonts(appConfig)));
};

export { declaredFonts, embedExpoAppAssets, requireFunction };
