import { afterEach, describe, expect, it } from "vitest";
import { mkdtempSync, readFileSync, rmSync, writeFileSync } from "node:fs";
import path from "node:path";
import { tmpdir } from "node:os";
import { writeBundleAssets } from "./bundle-assets.ts";

const FIRST_DIRECTORY = 0;
const BASE_SCALE = 1;
const DOUBLE_SCALE = 2;
const TRIPLE_SCALE = 3;
const directories: string[] = [];

const makeDirectory = (): string => {
  const directory = mkdtempSync(path.join(tmpdir(), "rnl-bundle-assets-"));

  directories.push(directory);

  return directory;
};

const writeSource = (directory: string, name: string): string => {
  const sourcePath = path.join(directory, name);

  writeFileSync(sourcePath, name);

  return sourcePath;
};

afterEach(() => {
  for (const directory of directories.splice(FIRST_DIRECTORY)) {
    rmSync(directory, { force: true, recursive: true });
  }
});

describe("writeBundleAssets", () => {
  it("writes each scale where AssetSourceResolver looks beside a file bundle, a pnpm store path included", () => {
    const sources = makeDirectory();
    const bundleDirectory = makeDirectory();
    const pnpmLocation =
      "/assets/../../node_modules/.pnpm/react-native@0.88.1/node_modules/react-native/Libraries/LogBox/UI/LogBoxImages";

    writeBundleAssets(
      [
        {
          files: [writeSource(sources, "close.png"), writeSource(sources, "close@2x.png")],
          httpServerLocation: pnpmLocation,
          name: "close",
          scales: [BASE_SCALE, DOUBLE_SCALE],
          type: "png",
        },
        {
          files: [writeSource(sources, "logo.png")],
          httpServerLocation: "/assets/src",
          name: "logo",
          scales: [TRIPLE_SCALE],
          type: "png",
        },
      ],
      bundleDirectory,
    );

    const store = path.join(
      bundleDirectory,
      "assets/__node_modules/.pnpm/react-native@0.88.1/node_modules/react-native/Libraries/LogBox/UI/LogBoxImages",
    );

    expect(readFileSync(path.join(store, "close.png"), "utf8")).toBe("close.png");
    expect(readFileSync(path.join(store, "close@2x.png"), "utf8")).toBe("close@2x.png");
    expect(readFileSync(path.join(bundleDirectory, "assets/src/logo@3x.png"), "utf8")).toBe("logo.png");
  });
});

describe("writeBundleAssets, a location that ends in ..", () => {
  it("turns each ../ of the whole path into _, a location that ends in .. included, as the runtime resolves it", () => {
    const sources = makeDirectory();
    const bundleDirectory = makeDirectory();

    writeBundleAssets(
      [
        {
          files: [writeSource(sources, "x.png")],
          httpServerLocation: "/assets/..",
          name: "x",
          scales: [BASE_SCALE],
          type: "png",
        },
        {
          files: [writeSource(sources, "y.png")],
          httpServerLocation: "/assets/../..",
          name: "x",
          scales: [BASE_SCALE],
          type: "png",
        },
      ],
      bundleDirectory,
    );

    expect(readFileSync(path.join(bundleDirectory, "assets/_x.png"), "utf8")).toBe("x.png");
    expect(readFileSync(path.join(bundleDirectory, "assets/__x.png"), "utf8")).toBe("y.png");
  });
});

describe("writeBundleAssets, Metro's asset data", () => {
  it("refuses a scale Metro lists no file for", () => {
    expect(() => {
      writeBundleAssets(
        [{ files: [], httpServerLocation: "/assets/src", name: "logo", scales: [BASE_SCALE], type: "png" }],
        makeDirectory(),
      );
    }).toThrow("Metro lists no file for scale 1 of /assets/src/logo");
  });
});
