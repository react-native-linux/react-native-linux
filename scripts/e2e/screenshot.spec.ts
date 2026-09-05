import { cropImage, findScreenshotFailure } from "./screenshot.ts";
import { describe, expect, it } from "vitest";

const CHANNELS_PER_PIXEL = 4;
const IMAGE_WIDTH = 20;
const IMAGE_HEIGHT = 20;
const OTHER_WIDTH = 10;
const OTHER_HEIGHT = 10;
const BACKGROUND = 100;
const WITHIN_TOLERANCE = 108;
const FAR_APART = 200;
const NONE = 0;
const NO_BUDGET = 1;
const GENEROUS_BUDGET = 50;
const MISSING_PIXELS = 3;
const DIFFERENT_PIXELS = 3;
const TOLERATED_PIXELS = 2;

interface PixelImage {
  readonly data: Uint8Array;
  readonly height: number;
  readonly width: number;
}

const buildImage = (width: number, height: number, channelValue: number): PixelImage => ({
  data: new Uint8Array(width * height * CHANNELS_PER_PIXEL).fill(channelValue),
  height,
  width,
});

const golden = buildImage(IMAGE_WIDTH, IMAGE_HEIGHT, BACKGROUND);

const withDifferentPixels = (pixelCount: number): PixelImage => {
  const image = buildImage(IMAGE_WIDTH, IMAGE_HEIGHT, BACKGROUND);

  image.data.fill(FAR_APART, NONE, pixelCount * CHANNELS_PER_PIXEL);

  return image;
};

/** A render that stopped short of the golden's last pixels, with everything it did write matching. */
const truncatedRender = (missingPixels: number): PixelImage => ({
  data: new Uint8Array((IMAGE_WIDTH * IMAGE_HEIGHT - missingPixels) * CHANNELS_PER_PIXEL).fill(BACKGROUND),
  height: IMAGE_HEIGHT,
  width: IMAGE_WIDTH,
});

describe("findScreenshotFailure dimensions", () => {
  it("reports a size mismatch before comparing any pixel", () => {
    expect(findScreenshotFailure(buildImage(OTHER_WIDTH, IMAGE_HEIGHT, BACKGROUND), golden, GENEROUS_BUDGET)).toBe(
      "the screenshot is 10x20 and the golden is 20x20",
    );
  });

  it("reports a height mismatch at an equal width", () => {
    expect(findScreenshotFailure(buildImage(IMAGE_WIDTH, OTHER_HEIGHT, BACKGROUND), golden, GENEROUS_BUDGET)).toBe(
      "the screenshot is 20x10 and the golden is 20x20",
    );
  });
});

describe("findScreenshotFailure tolerance", () => {
  it("reports nothing for identical pictures", () => {
    expect(findScreenshotFailure(buildImage(IMAGE_WIDTH, IMAGE_HEIGHT, BACKGROUND), golden, NO_BUDGET)).toBeNull();
  });

  it("tolerates a per-channel difference at the delta budget", () => {
    expect(
      findScreenshotFailure(buildImage(IMAGE_WIDTH, IMAGE_HEIGHT, WITHIN_TOLERANCE), golden, NO_BUDGET),
    ).toBeNull();
  });

  it("passes a difference inside the scenario's absolute budget", () => {
    expect(findScreenshotFailure(withDifferentPixels(TOLERATED_PIXELS), golden, GENEROUS_BUDGET)).toBeNull();
  });

  it("fails a difference over the scenario's absolute budget", () => {
    expect(findScreenshotFailure(withDifferentPixels(DIFFERENT_PIXELS), golden, NO_BUDGET)).toBe(
      "3 pixels differ by more than 8 per channel, the budget is 1",
    );
  });

  it("counts the pixels a truncated render never wrote", () => {
    expect(findScreenshotFailure(truncatedRender(MISSING_PIXELS), golden, NO_BUDGET)).toBe(
      "3 pixels differ by more than 8 per channel, the budget is 1",
    );
  });
});

const OUTER_WIDTH = 4;
const OUTER_HEIGHT = 4;
const GREEN_CHANNEL_OFFSET = 1;
const BLUE_CHANNEL_OFFSET = 2;
const ALPHA_CHANNEL_OFFSET = 3;
const NEXT_INDEX = 1;
const CROP_ORIGIN = 1;
const CROP_SIZE = 2;
const SINGLE_PIXEL = 1;
const LAST_INDEX = OUTER_WIDTH - NEXT_INDEX;
const OVERSIZED_LENGTH = 5;
const NO_OFFSET = 0;

/** Every pixel's red channel is its column, green its row — enough to prove a crop kept the right rectangle. */
const buildIndexedImage = (width: number, height: number): PixelImage => {
  const data = new Uint8Array(width * height * CHANNELS_PER_PIXEL);

  for (let row = NONE; row < height; row += NEXT_INDEX) {
    for (let column = NONE; column < width; column += NEXT_INDEX) {
      const offset = (row * width + column) * CHANNELS_PER_PIXEL;

      data[offset] = column;
      data[offset + GREEN_CHANNEL_OFFSET] = row;
      data[offset + BLUE_CHANNEL_OFFSET] = NONE;
      data[offset + ALPHA_CHANNEL_OFFSET] = NONE;
    }
  }

  return { data, height, width };
};

/** The bytes `buildIndexedImage` wrote for the pixel at `(column, row)`: red is the column, green the row. */
const indexedPixel = (column: number, row: number): readonly number[] => [column, row, NONE, NONE];

describe("cropImage", () => {
  const image = buildIndexedImage(OUTER_WIDTH, OUTER_HEIGHT);
  const nextColumn = CROP_ORIGIN + NEXT_INDEX;
  const nextRow = CROP_ORIGIN + NEXT_INDEX;

  it("extracts the named rectangle", () => {
    const cropped = cropImage(image, { height: CROP_SIZE, left: CROP_ORIGIN, top: CROP_ORIGIN, width: CROP_SIZE });

    expect(cropped).toEqual({
      data: Uint8Array.from([
        ...indexedPixel(CROP_ORIGIN, CROP_ORIGIN),
        ...indexedPixel(nextColumn, CROP_ORIGIN),
        ...indexedPixel(CROP_ORIGIN, nextRow),
        ...indexedPixel(nextColumn, nextRow),
      ]),
      height: CROP_SIZE,
      width: CROP_SIZE,
    });
  });

  it("extracts the whole image when the crop is the same size", () => {
    expect(cropImage(image, { height: OUTER_HEIGHT, left: NO_OFFSET, top: NO_OFFSET, width: OUTER_WIDTH })).toEqual(
      image,
    );
  });

  it("extracts a single pixel", () => {
    expect(cropImage(image, { height: SINGLE_PIXEL, left: LAST_INDEX, top: LAST_INDEX, width: SINGLE_PIXEL })).toEqual({
      data: Uint8Array.from(indexedPixel(LAST_INDEX, LAST_INDEX)),
      height: SINGLE_PIXEL,
      width: SINGLE_PIXEL,
    });
  });
});

describe("cropImage out of bounds", () => {
  const image = buildIndexedImage(OUTER_WIDTH, OUTER_HEIGHT);

  it("reports a crop wider than the image", () => {
    expect(cropImage(image, { height: SINGLE_PIXEL, left: NO_OFFSET, top: NO_OFFSET, width: OVERSIZED_LENGTH })).toBe(
      "the crop 5x1+0+0 does not fit inside the 4x4 screenshot",
    );
  });

  it("reports a crop taller than the image", () => {
    expect(cropImage(image, { height: OVERSIZED_LENGTH, left: NO_OFFSET, top: NO_OFFSET, width: SINGLE_PIXEL })).toBe(
      "the crop 1x5+0+0 does not fit inside the 4x4 screenshot",
    );
  });

  it("reports a crop that fits in size but is offset past the edge", () => {
    expect(cropImage(image, { height: CROP_SIZE, left: LAST_INDEX, top: LAST_INDEX, width: CROP_SIZE })).toBe(
      "the crop 2x2+3+3 does not fit inside the 4x4 screenshot",
    );
  });
});
