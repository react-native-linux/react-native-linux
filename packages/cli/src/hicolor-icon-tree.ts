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
  public constructor(message: string) {
    super(message);
    this.name = "NoSquareIconError";
  }
}

const noWidthSeenYet = -1;

const hicolorInstallPath = (applicationIdentifier: string, icon: IconSource): string => {
  const scale = icon.scale ?? standardScale;
  const nominalSize = icon.width / scale;
  const scaleSuffix = scale === doubleScale ? "@2" : "";
  return `usr/share/icons/hicolor/${nominalSize}x${nominalSize}${scaleSuffix}/apps/${applicationIdentifier}.png`;
};

const requireEverySourceSquare = (icons: readonly IconSource[]): void => {
  for (const icon of icons) {
    if (icon.width !== icon.height) {
      throw new NoSquareIconError(
        `Icon source "${icon.sourcePath}" is ${icon.width}x${icon.height}, not square; every hicolor source must be square`,
      );
    }
  }
};

const widestSourcePath = (icons: readonly IconSource[]): string => {
  let largestWidth = noWidthSeenYet;
  let largestSourcePath = "";
  for (const icon of icons) {
    if (icon.width > largestWidth) {
      largestWidth = icon.width;
      largestSourcePath = icon.sourcePath;
    }
  }

  if (largestWidth === noWidthSeenYet) {
    throw new NoSquareIconError("No icon sources were given; the hicolor tree requires at least one");
  }

  return largestSourcePath;
};

const layoutHicolorIconTree = (icons: readonly IconSource[], applicationIdentifier: string): HicolorIconTree => {
  requireEverySourceSquare(icons);

  const entries = icons.map((icon) => ({
    installPath: hicolorInstallPath(applicationIdentifier, icon),
    sourcePath: icon.sourcePath,
  }));

  return { directoryIconSourcePath: widestSourcePath(icons), entries };
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
