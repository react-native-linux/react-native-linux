const CHANNELS_PER_PIXEL = 4;
const NOT_FOUND_INDEX = -1;
const NONE = 0;
const ONE_PIXEL = 1;

interface PixelImage {
  readonly data: Uint8Array;
  readonly height: number;
  readonly width: number;
}

/**
 * The budget a fixture opts into in `fixtures.ts` when zero tolerance is provably wrong for it — see
 * `compareImagesWithTolerance`.
 */
interface ToleranceBudget {
  /** A channel this far from the golden's, or closer, does not count against `maxDifferentPixels`. */
  readonly maxChannelDifference: number;
  /** Pixels breaching `maxChannelDifference` up to this count are still a pass. */
  readonly maxDifferentPixels: number;
}

const formatPixel = (data: Uint8Array, offset: number): string =>
  [...data.subarray(offset, offset + CHANNELS_PER_PIXEL)].join(", ");

/**
 * Compares two decoded RGBA images and describes the first difference, or returns null when they are identical.
 *
 * The tolerance is zero, deliberately. The gospel's perceptual threshold exists because GPU rasterisation and font
 * hinting vary between drivers; this rig renders on the CPU through Skia's raster backend, where one Skia build
 * given one scene produces the same bytes on every machine. A tolerance here would only hide a real regression.
 * A GPU-path golden needs its own comparator, not a loosened version of this one.
 */
const compareImages = (actual: PixelImage, expected: PixelImage): string | null => {
  if (actual.width !== expected.width || actual.height !== expected.height) {
    return `rendered ${actual.width}x${actual.height}, golden is ${expected.width}x${expected.height}`;
  }

  const differingChannelOffset = expected.data.findIndex(
    (channelValue, channelOffset) => channelValue !== actual.data[channelOffset],
  );

  if (differingChannelOffset === NOT_FOUND_INDEX) {
    return null;
  }

  const pixelIndex = Math.floor(differingChannelOffset / CHANNELS_PER_PIXEL);
  const pixelOffset = pixelIndex * CHANNELS_PER_PIXEL;
  const column = pixelIndex % expected.width;
  const row = Math.floor(pixelIndex / expected.width);

  return `pixel (${column}, ${row}) rendered as rgba(${formatPixel(actual.data, pixelOffset)}), golden has rgba(${formatPixel(expected.data, pixelOffset)})`;
};

const pixelChannelDelta = (actual: PixelImage, expected: PixelImage, pixelIndex: number): number => {
  const offset = pixelIndex * CHANNELS_PER_PIXEL;
  let worstChannelDelta = NONE;

  for (let channel = NONE; channel < CHANNELS_PER_PIXEL; channel += ONE_PIXEL) {
    const channelDelta = Math.abs((expected.data[offset + channel] ?? NONE) - (actual.data[offset + channel] ?? NONE));

    worstChannelDelta = Math.max(worstChannelDelta, channelDelta);
  }

  return worstChannelDelta;
};

const collectToleranceDeltas = (actual: PixelImage, expected: PixelImage): readonly number[] => {
  const pixelCount = expected.width * expected.height;
  const pixelDeltas: number[] = [];

  for (let pixelIndex = NONE; pixelIndex < pixelCount; pixelIndex += ONE_PIXEL) {
    pixelDeltas.push(pixelChannelDelta(actual, expected, pixelIndex));
  }

  return pixelDeltas;
};

interface ToleranceSummary {
  readonly differentPixels: number;
  readonly worstPixelIndex: number;
}

const summariseToleranceDifferences = (
  pixelDeltas: readonly number[],
  tolerance: ToleranceBudget,
): ToleranceSummary => {
  let differentPixels = NONE;
  let worstChannelDelta = NONE;
  let worstPixelIndex = NOT_FOUND_INDEX;

  for (const [pixelIndex, pixelDelta] of pixelDeltas.entries()) {
    if (pixelDelta > tolerance.maxChannelDifference) {
      differentPixels += ONE_PIXEL;
    }

    if (pixelDelta > worstChannelDelta) {
      worstChannelDelta = pixelDelta;
      worstPixelIndex = pixelIndex;
    }
  }

  return { differentPixels, worstPixelIndex };
};

/**
 * Compares two decoded RGBA images against a per-fixture tolerance budget, for the one raster fixture where zero
 * tolerance is wrong rather than for the raster rig in general — see the module docblock on `compareImages`.
 *
 * `emoji.js` draws Noto Color Emoji's CBDT bitmap glyphs scaled by FreeType, and FreeType's bitmap scaler is not
 * byte-identical across builds: issue #307 measured a one-unit difference in one channel of one pixel between the
 * host that blessed `emoji.png` and a host with a newer FreeType. That is a rasteriser rounding step, the same
 * category `perceptual-diff.ts` exists for, not a picture that changed — but the raster rig has no driver
 * variance to budget for everywhere else, so the tolerance is scoped to this one fixture rather than folded into
 * `compareImages`'s default.
 */
const compareImagesWithTolerance = (
  actual: PixelImage,
  expected: PixelImage,
  tolerance: ToleranceBudget,
): string | null => {
  if (actual.width !== expected.width || actual.height !== expected.height) {
    return `rendered ${actual.width}x${actual.height}, golden is ${expected.width}x${expected.height}`;
  }

  const pixelDeltas = collectToleranceDeltas(actual, expected);
  const { differentPixels, worstPixelIndex } = summariseToleranceDifferences(pixelDeltas, tolerance);

  if (differentPixels <= tolerance.maxDifferentPixels) {
    return null;
  }

  const pixelOffset = worstPixelIndex * CHANNELS_PER_PIXEL;
  const column = worstPixelIndex % expected.width;
  const row = Math.floor(worstPixelIndex / expected.width);

  return (
    `${differentPixels} pixels differ by more than ${tolerance.maxChannelDifference} per channel, the budget is ` +
    `${tolerance.maxDifferentPixels}; the worst is pixel (${column}, ${row}) rendered as ` +
    `rgba(${formatPixel(actual.data, pixelOffset)}), golden has rgba(${formatPixel(expected.data, pixelOffset)})`
  );
};

export { compareImages, compareImagesWithTolerance };
export type { ToleranceBudget };
