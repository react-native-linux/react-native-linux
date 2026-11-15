import { describe, expect, it } from "vitest";
import { mkdtempSync, rmSync, writeFileSync } from "node:fs";
import { babelCacheVersion } from "./babel-cache-version.ts";
import path from "node:path";
import { tmpdir } from "node:os";

const versionAfterWriting = (projectRoot: string, fileName: string, contents: string): string => {
  writeFileSync(path.join(projectRoot, fileName), contents);

  return babelCacheVersion(projectRoot);
};

describe("babelCacheVersion", () => {
  it("changes when the application's Babel config changes", () => {
    const projectRoot = mkdtempSync(path.join(tmpdir(), "rnl-babel-cache-"));
    const earlierVersions = [
      babelCacheVersion(projectRoot),
      versionAfterWriting(projectRoot, "babel.config.js", "module.exports = {};"),
      versionAfterWriting(projectRoot, ".babelrc", '{ "plugins": [] }'),
    ];
    const workletsVersion = versionAfterWriting(
      projectRoot,
      ".babelrc",
      '{ "plugins": ["react-native-worklets/plugin"] }',
    );

    const versions = [...earlierVersions, workletsVersion];

    expect(new Set(versions).size).toBe(versions.length);
    expect(babelCacheVersion(projectRoot)).toBe(workletsVersion);
    rmSync(projectRoot, { force: true, recursive: true });
  });

  it("changes when a project-wide config that Babel merges with .babelrc changes", () => {
    const projectRoot = mkdtempSync(path.join(tmpdir(), "rnl-babel-cache-"));

    writeFileSync(path.join(projectRoot, ".babelrc"), '{ "plugins": [] }');

    const beforeProjectWideEdit = versionAfterWriting(projectRoot, "babel.config.js", "module.exports = {};");
    const afterProjectWideEdit = versionAfterWriting(
      projectRoot,
      "babel.config.js",
      'module.exports = { plugins: ["react-native-worklets/plugin"] };',
    );

    expect(afterProjectWideEdit).not.toBe(beforeProjectWideEdit);
    rmSync(projectRoot, { force: true, recursive: true });
  });
});
