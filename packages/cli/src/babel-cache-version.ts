import { existsSync, readFileSync } from "node:fs";
import { createHash } from "node:crypto";
import path from "node:path";

/**
 * The root Babel config files of an application. Babel merges a project-wide `babel.config.*` with a file-relative
 * `.babelrc*`, and @react-native/metro-babel-transformer's cache key hashes only the preset and itself, so Metro
 * would keep serving transforms made under an older config. Files a JavaScript config requires are not followed.
 */
const babelConfigFileNames = [
  ".babelrc",
  ".babelrc.js",
  ".babelrc.json",
  "babel.config.js",
  "babel.config.json",
  "babel.config.cjs",
  "babel.config.mjs",
];

/** A Metro `cacheVersion` that changes whenever any root Babel config of the application at `projectRoot` does. */
const babelCacheVersion = (projectRoot: string): string => {
  const hash = createHash("sha256");

  for (const fileName of babelConfigFileNames) {
    const configPath = path.join(projectRoot, fileName);

    if (existsSync(configPath)) {
      hash.update(fileName).update("\0").update(readFileSync(configPath)).update("\0");
    }
  }

  return hash.digest("hex");
};

export { babelCacheVersion };
