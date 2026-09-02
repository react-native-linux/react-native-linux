const CHANNELS_PER_PIXEL = 4;
const MISSING_CHANNEL = 0;
const NONE = 0;
const ONE_PIXEL = 1;
const PERCENT_DECIMALS = 3;
const PERCENT_SCALE = 100;

/**
 * A pixel counts as differing when any of its channels is more than this far from the golden's. Below it, the
 * difference is antialiasing coverage or a rounding step in the driver, not a picture that changed.
 */
const MAX_CHANNEL_DELTA = 8;

/**
 * The share of the image allowed to differ at all. 1% of an 800x600 frame is 4800 pixels, which is more than the
 * antialiased perimeter of every shape in the window fixtures put together and far less than the smallest shape
 * in them, so a missing, moved or recoloured element still fails.
 */
const MAX_DIFFERING_PIXEL_FRACTION = 0.01;

interface PixelImage {
  readonly data: Uint8Array;
  readonly height: number;
  readonly width: number;
}

interface DifferenceSummary {
  readonly differingPixels: number;
  readonly worstChannelDelta: number;
  readonly worstPixelIndex: number;
}

const formatPercent = (fraction: number): string => (fraction * PERCENT_SCALE).toFixed(PERCENT_DECIMALS);

const collectPixelDeltas = (actual: PixelImage, expected: PixelImage): readonly number[] => {
  const pixelDeltas: number[] = [];

  for (const [channelOffset, expectedChannel] of expected.data.entries()) {
    const actualChannel = actual.data[channelOffset] ?? MISSING_CHANNEL;
    const pixelIndex = Math.floor(channelOffset / CHANNELS_PER_PIXEL);

    pixelDeltas[pixelIndex] = Math.max(pixelDeltas[pixelIndex] ?? NONE, Math.abs(expectedChannel - actualChannel));
  }

  return pixelDeltas;
};

const summariseDifferences = (pixelDeltas: readonly number[]): DifferenceSummary => {
  let differingPixels = NONE;
  let worstChannelDelta = NONE;
  let worstPixelIndex = NONE;

  for (const [pixelIndex, pixelDelta] of pixelDeltas.entries()) {
    if (pixelDelta > MAX_CHANNEL_DELTA) {
      differingPixels += ONE_PIXEL;
    }

    if (pixelDelta > worstChannelDelta) {
      worstChannelDelta = pixelDelta;
      worstPixelIndex = pixelIndex;
    }
  }

  return { differingPixels, worstChannelDelta, worstPixelIndex };
};

/**
 * Compares two decoded RGBA images with a tolerance, and describes the difference when it exceeds the budget.
 *
 * This is the comparator for the window goldens, and it is deliberately not the exact-equality one `png-diff.ts`
 * uses for the raster goldens. Those are produced by Skia's raster backend, where one Skia build given one scene
 * produces the same bytes on every machine, so any tolerance there would hide a real regression. A window golden
 * comes out of Skia's Ganesh Vulkan backend through lavapipe, and is therefore a function of the Mesa version the
 * distribution ships, of which swapchain format the surface offered, and of a rasteriser whose antialiasing
 * coverage is not the raster backend's. Pinning that to a byte would pin the driver, not the renderer.
 *
 * The two thresholds do different jobs. `MAX_CHANNEL_DELTA` decides what counts as a differing pixel at all, so a
 * wrong colour, a swizzled channel order or a black frame is caught on the first pixel it touches.
 * `MAX_DIFFERING_PIXEL_FRACTION` bounds how many pixels may differ, so an edge that antialiases differently
 * passes and a shape that moved, vanished or never drew does not. Geometry to the pixel is the raster rig's job;
 * this rig's job is that the swapchain path produces the same picture at all.
 */
const compareImagesPerceptually = (actual: PixelImage, expected: PixelImage): string | null => {
  if (actual.width !== expected.width || actual.height !== expected.height) {
    return `the render is ${actual.width}x${actual.height} and the golden is ${expected.width}x${expected.height}`;
  }

  const pixelCount = expected.width * expected.height;
  const summary = summariseDifferences(collectPixelDeltas(actual, expected));
  const differingFraction = summary.differingPixels / pixelCount;

  if (differingFraction <= MAX_DIFFERING_PIXEL_FRACTION) {
    return null;
  }

  const column = summary.worstPixelIndex % expected.width;
  const row = Math.floor(summary.worstPixelIndex / expected.width);

  return (
    `${summary.differingPixels} of ${pixelCount} pixels differ by more than ${MAX_CHANNEL_DELTA} per channel ` +
    `(${formatPercent(differingFraction)}%, the budget is ${formatPercent(MAX_DIFFERING_PIXEL_FRACTION)}%); ` +
    `the worst is pixel (${column}, ${row}) at a delta of ${summary.worstChannelDelta}`
  );
};

export { compareImagesPerceptually };
