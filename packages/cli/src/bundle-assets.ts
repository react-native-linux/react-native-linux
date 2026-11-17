import { copyFileSync, mkdirSync } from "node:fs";
import path from "node:path";

const BASE_SCALE = 1;
const LEADING_SLASH_LENGTH = 1;

/** The fields `react-native bundle` reads from each `AssetData` that `Metro.runBuild({ assets: true })` returns. */
interface BundleAsset {
  readonly files: readonly string[];
  readonly httpServerLocation: string;
  readonly name: string;
  readonly scales: readonly number[];
  readonly type: string;
}

/**
 * The path React Native's `AssetSourceResolver.scaledAssetURLNearBundle` resolves an image `require` to beside a bundle
 * loaded from a file: `httpServerLocation` without its leading slash, then the file name, with each `../` in the whole
 * path as `_`. Upstream's `getAssetDestPathIOS` replaces only within the location, which differs for a location that
 * ends in `..`.
 */
const assetDestinationPath = (asset: BundleAsset, scale: number): string =>
  `${asset.httpServerLocation.slice(LEADING_SLASH_LENGTH)}/${asset.name}${scale === BASE_SCALE ? "" : `@${String(scale)}x`}.${asset.type}`.replaceAll(
    "../",
    "_",
  );

/**
 * Copies every scale of the bundle's assets into `bundleDirectory`, as `react-native bundle --assets-dest
 * <bundleDirectory>` does after the same `Metro.runBuild`. `assets/fonts` and `assets/app.config` are reserved there:
 * `embedExpoAppAssets` replaces them, so an image a project keeps under a top-level `fonts` directory, or one named
 * `app.config`, would not survive it.
 */
const writeBundleAssets = (assets: readonly BundleAsset[], bundleDirectory: string): void => {
  for (const asset of assets) {
    for (const [index, scale] of asset.scales.entries()) {
      const sourcePath = asset.files[index] ?? null;

      if (sourcePath === null) {
        throw new Error(`Metro lists no file for scale ${String(scale)} of ${asset.httpServerLocation}/${asset.name}`);
      }

      const destinationPath = path.join(bundleDirectory, assetDestinationPath(asset, scale));

      mkdirSync(path.dirname(destinationPath), { recursive: true });
      copyFileSync(sourcePath, destinationPath);
    }
  }
};

export { writeBundleAssets };
