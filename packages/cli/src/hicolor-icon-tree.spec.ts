import {
  NoSquareIconError,
  doubleScale,
  layoutHicolorIconTree,
  loadIconSource,
  readPngDimensions,
  standardScale,
} from "./hicolor-icon-tree.ts";
import { describe, expect, it } from "vitest";
import { mkdtempSync, rmSync, writeFileSync } from "node:fs";
import { PNG } from "pngjs";
import path from "node:path";
import { tmpdir } from "node:os";

type TestIconScale = typeof standardScale | typeof doubleScale;

interface TestIconSource {
  readonly width: number;
  readonly height: number;
  readonly sourcePath: string;
  readonly scale?: TestIconScale;
}

const applicationIdentifier = "org.reactnativelinux.Flagship";
const firstEntryIndex = 0;

const iconSize64 = 64;
const iconSize128 = 128;
const iconSize256 = 256;
const iconSize96 = 96;
const wideIconWidth = 512;
const wideIconHeight = 300;

const squareIcon = (size: number, sourcePath: string, scale?: TestIconScale): TestIconSource => ({
  height: size,
  sourcePath,
  width: size,
  ...(typeof scale === "number" ? { scale } : {}),
});

const encodePng = (width: number, height: number): Buffer => PNG.sync.write(new PNG({ height, width }));

const withTemporaryDirectory = (namePrefix: string, run: (directory: string) => void): void => {
  const directory = mkdtempSync(path.join(tmpdir(), namePrefix));
  try {
    run(directory);
  } finally {
    rmSync(directory, { force: true, recursive: true });
  }
};

describe("layoutHicolorIconTree, mixed square sizes", () => {
  it("lays out each square icon at the standard hicolor path", () => {
    const icons = [
      squareIcon(iconSize128, "/src/icon-128.png", standardScale),
      squareIcon(iconSize256, "/src/icon-256.png"),
    ];

    const tree = layoutHicolorIconTree(icons, applicationIdentifier);

    expect(tree.entries).toEqual([
      {
        installPath: "usr/share/icons/hicolor/128x128/apps/org.reactnativelinux.Flagship.png",
        sourcePath: "/src/icon-128.png",
      },
      {
        installPath: "usr/share/icons/hicolor/256x256/apps/org.reactnativelinux.Flagship.png",
        sourcePath: "/src/icon-256.png",
      },
    ]);
  });

  it("suffixes the install path with @2 for a scale-2 source", () => {
    const tree = layoutHicolorIconTree(
      [squareIcon(iconSize256, "/src/icon-256@2.png", doubleScale)],
      applicationIdentifier,
    );

    expect(tree.entries[firstEntryIndex]?.installPath).toBe(
      "usr/share/icons/hicolor/256x256@2/apps/org.reactnativelinux.Flagship.png",
    );
  });
});

describe("layoutHicolorIconTree, the directory icon and the no-square-icon failure", () => {
  it("picks the largest square icon as the directory icon, ignoring non-square sources", () => {
    const icons = [
      squareIcon(iconSize128, "/src/icon-128.png"),
      { height: wideIconHeight, sourcePath: "/src/icon-wide.png", width: wideIconWidth },
      squareIcon(iconSize256, "/src/icon-256.png"),
      squareIcon(iconSize64, "/src/icon-64.png"),
    ];

    const tree = layoutHicolorIconTree(icons, applicationIdentifier);

    expect(tree.directoryIconSourcePath).toBe("/src/icon-256.png");
    expect(tree.entries).toHaveLength(icons.length);
  });

  it("fails by name, not by panic, when no source icon is square", () => {
    const icons = [{ height: wideIconHeight, sourcePath: "/src/icon-wide.png", width: wideIconWidth }];

    expect(() => layoutHicolorIconTree(icons, applicationIdentifier)).toThrow(NoSquareIconError);
  });

  it("reports the source count in the no-square-icon error message", () => {
    const icons = [
      { height: wideIconHeight, sourcePath: "/src/icon-wide.png", width: wideIconWidth },
      { height: iconSize128, sourcePath: "/src/icon-tall.png", width: iconSize64 },
    ];

    expect(() => layoutHicolorIconTree(icons, applicationIdentifier)).toThrow(`among ${icons.length} source(s)`);
  });
});

describe("readPngDimensions", () => {
  it("reads the real width and height out of a PNG buffer", () => {
    expect(readPngDimensions(encodePng(iconSize64, iconSize128))).toEqual({ height: iconSize128, width: iconSize64 });
  });
});

describe("loadIconSource", () => {
  it("reads a PNG file from disk and reports its real dimensions", () => {
    withTemporaryDirectory("hicolor-icon-", (directory) => {
      const sourcePath = path.join(directory, "icon.png");
      writeFileSync(sourcePath, encodePng(iconSize64, iconSize64));

      expect(loadIconSource(sourcePath)).toEqual({ height: iconSize64, sourcePath, width: iconSize64 });
    });
  });

  it("carries the scale through when one is given", () => {
    withTemporaryDirectory("hicolor-icon-", (directory) => {
      const sourcePath = path.join(directory, "icon@2.png");
      writeFileSync(sourcePath, encodePng(iconSize96, iconSize96));

      expect(loadIconSource(sourcePath, doubleScale)).toEqual({
        height: iconSize96,
        scale: doubleScale,
        sourcePath,
        width: iconSize96,
      });
    });
  });
});
