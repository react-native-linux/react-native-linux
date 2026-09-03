import { describe, expect, it } from "vitest";
import { findScreenshotFailure } from "./screenshot.ts";

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
