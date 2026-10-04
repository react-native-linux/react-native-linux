import { harnessMetroConfig, repositoryRoot } from "./metro-config.ts";
import Metro from "metro";
import path from "node:path";

const COMMAND_ARGUMENTS_START = 2;
const [outputPath = path.join(repositoryRoot, "build", "test-harness", "index.linux.bundle.js")] =
  process.argv.slice(COMMAND_ARGUMENTS_START);

await Metro.runBuild(await Metro.loadConfig({}, harnessMetroConfig), {
  dev: false,
  entry: "index.ts",
  minify: false,
  out: outputPath,
  platform: "linux",
});
