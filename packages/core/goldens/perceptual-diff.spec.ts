import { describe, expect, it } from "vitest";

import { compareImagesPerceptually } from "./perceptual-diff.ts";

const channelsPerPixel = 4;
const singleRow = 1;
const onePixelWide = 1;
const twoPixelsWide = 2;
const twoRows = 2;
const truncatedChannelCount = 4;

const background = 100;
const withinTolerance = 108;
const farApart = 200;
const nearBlack = 10;

interface PixelImage {
  readonly data: Uint8Array;
  readonly height: number;
  readonly width: number;
}

const buildImage = (width: number, height: number, channelValue: number): PixelImage => ({
  data: new Uint8Array(width * height * channelsPerPixel).fill(channelValue),
  height,
  width,
});

describe("compareImagesPerceptually dimensions", () => {
  it("reports a width mismatch before comparing any pixel", () => {
    expect(
      compareImagesPerceptually(
        buildImage(onePixelWide, singleRow, background),
        buildImage(twoPixelsWide, singleRow, farApart),
      ),
    ).toBe("the render is 1x1 and the golden is 2x1");
  });

  it("reports a height mismatch at an equal width", () => {
    expect(
      compareImagesPerceptually(
        buildImage(twoPixelsWide, singleRow, background),
        buildImage(twoPixelsWide, twoRows, background),
      ),
    ).toBe("the render is 2x1 and the golden is 2x2");
  });
});

describe("compareImagesPerceptually tolerance", () => {
  it("reports no difference between identical images", () => {
    expect(
      compareImagesPerceptually(
        buildImage(twoPixelsWide, singleRow, background),
        buildImage(twoPixelsWide, singleRow, background),
      ),
    ).toBeNull();
  });

  it("tolerates a per-channel difference at the delta budget", () => {
    expect(
      compareImagesPerceptually(
        buildImage(twoPixelsWide, singleRow, withinTolerance),
        buildImage(twoPixelsWide, singleRow, background),
      ),
    ).toBeNull();
  });

  it("reports the differing share and the worst pixel once the budget is exceeded", () => {
    expect(
      compareImagesPerceptually(
        buildImage(twoPixelsWide, singleRow, farApart),
        buildImage(twoPixelsWide, singleRow, nearBlack),
      ),
    ).toBe(
      "2 of 2 pixels differ by more than 8 per channel (100.000%, the budget is 1.000%); " +
        "the worst is pixel (0, 0) at a delta of 190",
    );
  });

  it("treats channels missing from a truncated render as absent colour", () => {
    const truncated: PixelImage = {
      data: new Uint8Array(truncatedChannelCount).fill(farApart),
      height: singleRow,
      width: twoPixelsWide,
    };

    expect(compareImagesPerceptually(truncated, buildImage(twoPixelsWide, singleRow, farApart))).toBe(
      "1 of 2 pixels differ by more than 8 per channel (50.000%, the budget is 1.000%); " +
        "the worst is pixel (1, 0) at a delta of 200",
    );
  });
});
