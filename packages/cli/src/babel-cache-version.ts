import { existsSync, readFileSync } from "node:fs";
import { createHash } from "node:crypto";
import path from "node:path";

/**
 * The project Babel config files @react-native/metro-babel-transformer reads, in its order. Its cache key hashes
 * only the preset and itself, so Metro would keep serving transforms made under an older config.
 */
const babelConfigFileNames = [".babelrc", ".babelrc.js", "babel.config.js"];

/** A Metro `cacheVersion` that changes whenever the Babel config of the application at `projectRoot` does. */
const babelCacheVersion = (projectRoot: string): string => {
  const configPath = babelConfigFileNames
    .map((fileName) => path.join(projectRoot, fileName))
    .find((candidate) => existsSync(candidate));

  return configPath ? createHash("sha256").update(readFileSync(configPath)).digest("hex") : "";
};

export { babelCacheVersion };
