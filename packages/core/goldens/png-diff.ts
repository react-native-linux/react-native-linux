const CHANNELS_PER_PIXEL = 4;
const NOT_FOUND_INDEX = -1;

interface PixelImage {
  readonly data: Uint8Array;
  readonly height: number;
  readonly width: number;
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

export { compareImages };
