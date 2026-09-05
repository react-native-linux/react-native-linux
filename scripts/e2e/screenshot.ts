const CHANNELS_PER_PIXEL = 4;
const LAST_CHANNEL_IN_PIXEL = 3;
const MISSING_CHANNEL = 0;
const NONE = 0;
const ONE_PIXEL = 1;

/**
 * The same per-channel tolerance `packages/core/goldens/perceptual-diff.ts` uses on the window goldens, and for
 * the same reason: a lavapipe frame's antialiasing coverage is a function of the Mesa version, not of the
 * renderer. That comparator itself is not reachable from here — oxlint's `import/no-relative-parent-imports`
 * forbids a root script reaching into a package — and it would not be the right budget anyway: it allows 1% of
 * the frame to differ, which at cage's output size is thousands of pixels, where an e2e golden is held to the
 * absolute count its scenario declares.
 */
const MAX_CHANNEL_DELTA = 8;

interface PixelImage {
  readonly data: Uint8Array;
  readonly height: number;
  readonly width: number;
}

interface Crop {
  readonly height: number;
  readonly left: number;
  readonly top: number;
  readonly width: number;
}

/**
 * Narrows a captured screenshot to the rectangle a scenario names, so an unrelated change elsewhere on the page
 * does not invalidate a golden. The golden itself is stored already cropped to that same rectangle, so only the
 * captured screenshot needs this before `findScreenshotFailure` compares the two.
 */
const cropImage = (image: PixelImage, crop: Crop): PixelImage | string => {
  if (crop.left + crop.width > image.width || crop.top + crop.height > image.height) {
    return (
      `the crop ${String(crop.width)}x${String(crop.height)}+${String(crop.left)}+${String(crop.top)} does not fit ` +
      `inside the ${String(image.width)}x${String(image.height)} screenshot`
    );
  }

  const rowBytes = crop.width * CHANNELS_PER_PIXEL;
  const data = new Uint8Array(rowBytes * crop.height);

  for (let row = NONE; row < crop.height; row += ONE_PIXEL) {
    const sourceStart = ((crop.top + row) * image.width + crop.left) * CHANNELS_PER_PIXEL;

    data.set(image.data.subarray(sourceStart, sourceStart + rowBytes), row * rowBytes);
  }

  return { data, height: crop.height, width: crop.width };
};

/**
 * Channels missing from a truncated render count as absent colour: a picture that stopped early differs from the
 * golden everywhere it stopped, rather than matching it by running out of bytes to disagree with.
 */
const countDifferentPixels = (actual: PixelImage, expected: PixelImage): number => {
  let differentPixels = NONE;
  let pixelDelta = NONE;

  for (const [channelOffset, expectedChannel] of expected.data.entries()) {
    const actualChannel = actual.data[channelOffset] ?? MISSING_CHANNEL;
    pixelDelta = Math.max(pixelDelta, Math.abs(expectedChannel - actualChannel));

    if (channelOffset % CHANNELS_PER_PIXEL === LAST_CHANNEL_IN_PIXEL) {
      if (pixelDelta > MAX_CHANNEL_DELTA) {
        differentPixels += ONE_PIXEL;
      }

      pixelDelta = NONE;
    }
  }

  return differentPixels;
};

const findScreenshotFailure = (actual: PixelImage, expected: PixelImage, maxDifferentPixels: number): string | null => {
  if (actual.width !== expected.width || actual.height !== expected.height) {
    return `the screenshot is ${String(actual.width)}x${String(actual.height)} and the golden is ${String(expected.width)}x${String(expected.height)}`;
  }

  const differentPixels = countDifferentPixels(actual, expected);

  if (differentPixels <= maxDifferentPixels) {
    return null;
  }

  return (
    `${String(differentPixels)} pixels differ by more than ${String(MAX_CHANNEL_DELTA)} per channel, ` +
    `the budget is ${String(maxDifferentPixels)}`
  );
};

export { cropImage, findScreenshotFailure };
export type { Crop };
