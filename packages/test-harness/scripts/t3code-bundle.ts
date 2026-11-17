import Metro from "metro";
import path from "node:path";
import { withLinuxLayer } from "./t3code-metro-config.ts";
import { writeBundleAssets } from "@react-native-linux/cli/bundle-assets.ts";

/*
 * T3 Code's own development bundle for platform linux: `node t3code-bundle.ts <t3code checkout> <output>`. The
 * application's Metro config is loaded from its checkout as `expo start` loads it, then scripts/t3code-fixture.ts runs
 * the result, with the bundle's images beside it as `react-native bundle --assets-dest` writes them (#808). It is a
 * development bundle because T3 Code pairs from a link only when `__DEV__` is set.
 */
const COMMAND_ARGUMENTS_START = 2;
const [checkout = "", outputPath = ""] = process.argv.slice(COMMAND_ARGUMENTS_START);
const mobileRoot = path.join(path.resolve(checkout), "apps", "mobile");

const { assets = [] } = await Metro.runBuild(withLinuxLayer(await Metro.loadConfig({ cwd: mobileRoot }), mobileRoot), {
  assets: true,
  // Expo picks the Hermes transform profile per platform, and names no profile for one it does not know.
  customTransformOptions: { engine: "hermes" },
  dev: true,
  entry: path.join(mobileRoot, "index.ts"),
  minify: false,
  out: outputPath,
  platform: "linux",
  unstable_transformProfile: "hermes-stable",
});

writeBundleAssets(assets, path.dirname(outputPath));
