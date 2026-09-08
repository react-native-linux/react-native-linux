import { PNG } from "pngjs";

import { readFileSync } from "node:fs";

const standardScale = 1;
const doubleScale = 2;
type IconScale = typeof standardScale | typeof doubleScale;

interface IconSource {
  readonly width: number;
  readonly height: number;
  readonly sourcePath: string;
  readonly scale?: IconScale;
}

interface HicolorIconEntry {
  readonly installPath: string;
  readonly sourcePath: string;
}

interface HicolorIconTree {
  readonly entries: readonly HicolorIconEntry[];
  readonly directoryIconSourcePath: string;
}

class NoSquareIconError extends Error {
  public constructor(sourceCount: number) {
    super(`No square icon found among ${sourceCount} source(s); the hicolor tree requires at least one`);
    this.name = "NoSquareIconError";
  }
}

const noWidthSeenYet = -1;

const hicolorInstallPath = (applicationIdentifier: string, icon: IconSource): string => {
  const scaleSuffix = icon.scale === doubleScale ? "@2" : "";
  return `usr/share/icons/hicolor/${icon.width}x${icon.height}${scaleSuffix}/apps/${applicationIdentifier}.png`;
};

const layoutHicolorIconTree = (icons: readonly IconSource[], applicationIdentifier: string): HicolorIconTree => {
  const entries = icons.map((icon) => ({
    installPath: hicolorInstallPath(applicationIdentifier, icon),
    sourcePath: icon.sourcePath,
  }));

  let largestSquareWidth = noWidthSeenYet;
  let largestSquareSourcePath = "";
  for (const icon of icons) {
    if (icon.width === icon.height && icon.width > largestSquareWidth) {
      largestSquareWidth = icon.width;
      largestSquareSourcePath = icon.sourcePath;
    }
  }

  if (largestSquareWidth === noWidthSeenYet) {
    throw new NoSquareIconError(icons.length);
  }

  return { directoryIconSourcePath: largestSquareSourcePath, entries };
};

const readPngDimensions = (pngBuffer: Buffer): { readonly width: number; readonly height: number } => {
  const png = PNG.sync.read(pngBuffer);
  return { height: png.height, width: png.width };
};

const loadIconSource = (sourcePath: string, scale?: IconScale): IconSource => {
  const { height, width } = readPngDimensions(readFileSync(sourcePath));
  return typeof scale === "number" ? { height, scale, sourcePath, width } : { height, sourcePath, width };
};

export type { HicolorIconEntry, HicolorIconTree, IconScale, IconSource };
export { doubleScale, layoutHicolorIconTree, loadIconSource, NoSquareIconError, readPngDimensions, standardScale };
