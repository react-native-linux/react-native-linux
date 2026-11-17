import { harnessMetroConfig, repositoryRoot } from "./metro-config.ts";
import Metro from "metro";
import path from "node:path";
import { writeBundleAssets } from "@react-native-linux/cli/bundle-assets.ts";

const COMMAND_ARGUMENTS_START = 2;
const RELEASE_FLAG = "--release";
const commandArguments = process.argv.slice(COMMAND_ARGUMENTS_START);
// The entry defaults to the harness app; an e2e scenario names its own React application entry instead.
const [outputPath = path.join(repositoryRoot, "build", "test-harness", "index.linux.bundle.js"), entry = "index.ts"] =
  commandArguments.filter((argument) => argument !== RELEASE_FLAG);

const { assets = [] } = await Metro.runBuild(await Metro.loadConfig({}, harnessMetroConfig), {
  assets: true,
  dev: false,
  entry,
  // #82: `--release` is the production bundle a shipped app runs, minified as `react-native bundle --dev false` is.
  minify: commandArguments.includes(RELEASE_FLAG),
  out: outputPath,
  platform: "linux",
});

writeBundleAssets(assets, path.dirname(outputPath));
