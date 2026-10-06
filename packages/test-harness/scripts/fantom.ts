import { execFileSync, spawnSync } from "node:child_process";
import { existsSync, mkdirSync, readFileSync, realpathSync, writeFileSync } from "node:fs";
import { gradeFantomRuns, readExpectations } from "@react-native-linux/cli/fantom-expectations.ts";
import { harnessMetroConfig, repositoryRoot } from "./metro-config.ts";
import Metro from "metro";
import path from "node:path";

/**
 * #210/#423: upstream `*-itest.js` files, unmodified, through upstream Fantom's own in-runtime harness
 * (`describe`/`it`/`expect` from `private/react-native-fantom/runtime/setup.js`), bundled by Metro for `linux` and
 * run in `hello_react`. Fantom's sources are a sparse clone of the vendored tag; the itests are the vendored tree's.
 *
 * The corpus is `packages/core/fantom-expectations.json`: every suite it names runs, and every failure it lists is
 * a known gap with the issue that owns it. Anything else that fails, or a listed failure that passes, fails the run.
 *
 * node packages/test-harness/scripts/fantom.ts [itest path relative to packages/react-native/src ...]
 */

const COMMAND_ARGUMENTS_START = 2;
const NONE = 0;
const SUCCESS_EXIT_STATUS = 0;
const FAILURE_EXIT_STATUS = 1;
/** Real paths: Metro's file map knows only real files under its watch folders, and `third_party` may be a symlink. */
const vendoredReactNative = realpathSync(
  path.join(repositoryRoot, "third_party", "react-native", "packages", "react-native"),
);
const bundledReactNative = realpathSync(
  path.join(repositoryRoot, "packages", "test-harness", "node_modules", "react-native"),
);
const fantomSource = path.join(repositoryRoot, "build", "fantom-source");
const fantomPackage = path.join(fantomSource, "private", "react-native-fantom");
const workDirectory = path.join(repositoryRoot, "build", "fantom");
const binaryPath = path.join(repositoryRoot, "build", "dev", "bin", "hello_react");
const expectationsPath = path.join(repositoryRoot, "packages", "core", "fantom-expectations.json");
const resultPrefix = "[fantom] ";

const cloneFantom = (tag: string): void => {
  if (existsSync(fantomPackage)) {
    return;
  }

  const repository = "https://github.com/facebook/react-native.git";

  execFileSync("git", [
    "clone",
    "--filter=blob:none",
    "--sparse",
    "--depth",
    "1",
    "--branch",
    tag,
    repository,
    fantomSource,
  ]);
  execFileSync("git", ["-C", fantomSource, "sparse-checkout", "set", "--cone", "private/react-native-fantom"]);
};

const writeEntry = (testPath: string): string => {
  const entryPath = path.join(workDirectory, `${path.basename(testPath, ".js")}.entry.js`);

  writeFileSync(
    entryPath,
    [
      "import {registerTest} from '@react-native/fantom/runtime/setup';",
      "import {setConstants} from '@react-native/fantom/src/Constants';",
      "setConstants({isOSS: true, isRunningFromCI: false, runBenchmarks: false, fantomConfigSummary: '', " +
        "jsHeapSnapshotOutputPathTemplate: '', jsHeapSnapshotOutputPathTemplateToken: '', jsTraceOutputPath: null, " +
        "hostPlatform: 'linux'});",
      `registerTest(() => require(${JSON.stringify(testPath)}), {updateSnapshot: 'none', data: {}});`,
      "",
    ].join("\n"),
  );

  return entryPath;
};

const reactNativePrefix = "react-native/";

/**
 * Where a specifier may live, in order: as written; a `react-native/...` path the installed package does not ship
 * (it leaves `src/private/testing` out), against the vendored tree; and a relative path from a vendored file to a
 * directory the sparse vendored tree does not have (`Libraries/`), against the installed package.
 */
const candidateSpecifiers = (moduleName: string, originModulePath: string): readonly string[] => [
  moduleName,
  ...(moduleName.startsWith(reactNativePrefix)
    ? [path.join(vendoredReactNative, moduleName.slice(reactNativePrefix.length))]
    : []),
  ...(moduleName.startsWith(".") && originModulePath.startsWith(vendoredReactNative)
    ? [path.resolve(path.dirname(originModulePath), moduleName).replace(vendoredReactNative, bundledReactNative)]
    : []),
];

const resolveFirst = <Resolution>(
  candidates: readonly string[],
  resolve: (specifier: string) => Resolution,
): Resolution => {
  const [first = "", ...rest] = candidates;

  try {
    return resolve(first);
  } catch (error) {
    if (rest.length === NONE) {
      throw error;
    }

    return resolveFirst(rest, resolve);
  }
};

/**
 * The itest lives in the vendored tree, but every module it reaches has to be the one copy of `react-native` the
 * rest of the bundle uses, or its singletons split in two. Anything resolved into the vendored package other than
 * the test itself is redirected to the same path in the installed package, which is the same tag, when the
 * installed package ships it.
 *
 * React Native's default config blocks every `__tests__` path, which is exactly where the itests live; the
 * replacement is a pattern that matches nothing rather than an empty list, because Metro joins the list into one
 * regex and an empty one matches everything. And nothing runs ahead of the entry, as in Fantom's own config: a test
 * sets the environment up itself, and one asserts it was not set up before it ran, which InitializeCore would do.
 */
const fantomConfig = (testPath: string): typeof harnessMetroConfig => {
  const harnessResolve = harnessMetroConfig.resolver.resolveRequest;

  return {
    ...harnessMetroConfig,
    resolver: {
      ...harnessMetroConfig.resolver,
      blockList: /(?!)/u,
      extraNodeModules: { "@react-native/fantom": fantomPackage },
      resolveRequest: (context, moduleName, platform) => {
        const resolve = harnessResolve ?? context.resolveRequest;
        const resolution = resolveFirst(candidateSpecifiers(moduleName, context.originModulePath), (specifier) =>
          resolve(context, specifier, platform),
        );

        if (resolution.type !== "sourceFile" || !resolution.filePath.startsWith(vendoredReactNative)) {
          return resolution;
        }

        const installed = resolution.filePath.replace(vendoredReactNative, bundledReactNative);

        return resolution.filePath !== testPath && existsSync(installed)
          ? { ...resolution, filePath: installed }
          : resolution;
      },
    },
    serializer: { ...harnessMetroConfig.serializer, getModulesRunBeforeMainModule: () => [] },
    watchFolders: [...harnessMetroConfig.watchFolders, fantomSource, vendoredReactNative],
  };
};

const runItest = async (relativePath: string): Promise<unknown> => {
  const testPath = path.join(vendoredReactNative, "src", relativePath);
  const bundlePath = path.join(workDirectory, `${path.basename(testPath, ".js")}.bundle.js`);

  await Metro.runBuild(await Metro.loadConfig({}, fantomConfig(testPath)), {
    dev: true,
    entry: writeEntry(testPath),
    minify: false,
    out: bundlePath,
    platform: "linux",
  });

  // A spawn, not execFileSync: a fatal error after the suite reported must not lose the result.
  const output = spawnSync(binaryPath, ["--fantom", bundlePath], { encoding: "utf8" }).stdout;
  const resultLine = output.split("\n").find((line) => line.startsWith(resultPrefix)) ?? null;

  return resultLine === null
    ? { error: { message: "the suite reported no result" } }
    : JSON.parse(resultLine.slice(resultPrefix.length));
};

const vendorLock: unknown = JSON.parse(readFileSync(path.join(repositoryRoot, "scripts", "vendor.lock.json"), "utf8"));

mkdirSync(workDirectory, { recursive: true });
cloneFantom(typeof vendorLock === "object" && vendorLock !== null && "tag" in vendorLock ? String(vendorLock.tag) : "");

const expectations = readExpectations(JSON.parse(readFileSync(expectationsPath, "utf8")));
const requested = process.argv.slice(COMMAND_ARGUMENTS_START);
const runs = [];

for (const suite of requested.length > NONE ? requested : Object.keys(expectations)) {
  const result = await runItest(suite);

  process.stdout.write(`${JSON.stringify({ result, suite })}\n`);
  runs.push({ result, suite });
}

const problems = gradeFantomRuns(runs, expectations);

process.stderr.write(
  problems.length > NONE ? `${problems.join("\n")}\n` : `fantom: ${String(runs.length)} suites as expected\n`,
);
process.exitCode = problems.length > NONE ? FAILURE_EXIT_STATUS : SUCCESS_EXIT_STATUS;
