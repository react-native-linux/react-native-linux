import { compareImages, compareImagesWithTolerance } from "./png-diff.ts";

import { describe, expect, it } from "vitest";

const squareSide = 2;
const narrowWidth = 1;
const shortHeight = 1;
const lastPixelIndex = 3;
const secondPixelIndex = 1;
const channelsPerPixel = 4;
const hexadecimalRadix = 16;
const channelPattern = /[\dA-F]{2}/gu;

const blueRgba = "3366CCFF";
const redRgba = "CC3333FF";

const oneChannelOffRgba = "3366CBFF";
const wildlyOffRgba = "FF0000FF";

const oneOffOnePixelBudget = { maxChannelDifference: 1, maxDifferentPixels: 1 };

const truncationStart = 0;
const truncatedChannelCount = 1;

const toChannels = (hex: string): number[] =>
  (hex.match(channelPattern) ?? []).map((channel) => Number.parseInt(channel, hexadecimalRadix));

const buildImage = (
  width: number,
  height: number,
  pixelHex: string,
): { data: Uint8Array; height: number; width: number } => ({
  data: Uint8Array.from(toChannels(pixelHex.repeat(width * height))),
  height,
  width,
});

describe("compareImages", () => {
  it("reports no difference between identical images", () => {
    expect(
      compareImages(buildImage(squareSide, squareSide, blueRgba), buildImage(squareSide, squareSide, blueRgba)),
    ).toBeNull();
  });

  it("reports a width mismatch before comparing any pixel", () => {
    expect(
      compareImages(buildImage(narrowWidth, squareSide, blueRgba), buildImage(squareSide, squareSide, redRgba)),
    ).toBe("rendered 1x2, golden is 2x2");
  });

  it("reports a height mismatch at an equal width", () => {
    expect(
      compareImages(buildImage(squareSide, shortHeight, blueRgba), buildImage(squareSide, squareSide, blueRgba)),
    ).toBe("rendered 2x1, golden is 2x2");
  });

  it("locates the first differing pixel by its coordinates", () => {
    const rendered = buildImage(squareSide, squareSide, blueRgba);
    const golden = buildImage(squareSide, squareSide, blueRgba);

    golden.data.set(toChannels(redRgba), lastPixelIndex * channelsPerPixel);

    expect(compareImages(rendered, golden)).toBe(
      "pixel (1, 1) rendered as rgba(51, 102, 204, 255), golden has rgba(204, 51, 51, 255)",
    );
  });
});

describe("compareImagesWithTolerance dimensions", () => {
  it("reports a width mismatch before comparing any pixel", () => {
    expect(
      compareImagesWithTolerance(
        buildImage(narrowWidth, squareSide, blueRgba),
        buildImage(squareSide, squareSide, redRgba),
        oneOffOnePixelBudget,
      ),
    ).toBe("rendered 1x2, golden is 2x2");
  });

  it("reports a height mismatch at an equal width", () => {
    expect(
      compareImagesWithTolerance(
        buildImage(squareSide, shortHeight, blueRgba),
        buildImage(squareSide, squareSide, blueRgba),
        oneOffOnePixelBudget,
      ),
    ).toBe("rendered 2x1, golden is 2x2");
  });
});

describe("compareImagesWithTolerance budget", () => {
  it("reports no difference between identical images", () => {
    expect(
      compareImagesWithTolerance(
        buildImage(squareSide, squareSide, blueRgba),
        buildImage(squareSide, squareSide, blueRgba),
        oneOffOnePixelBudget,
      ),
    ).toBeNull();
  });

  it("does not count a channel delta at the tolerance threshold against the pixel budget", () => {
    const rendered = buildImage(squareSide, squareSide, blueRgba);
    const golden = buildImage(squareSide, squareSide, blueRgba);

    golden.data.set(toChannels(oneChannelOffRgba), lastPixelIndex * channelsPerPixel);

    expect(compareImagesWithTolerance(rendered, golden, oneOffOnePixelBudget)).toBeNull();
  });

  it("tolerates as many pixels over budget as the budget allows", () => {
    const rendered = buildImage(squareSide, squareSide, blueRgba);
    const golden = buildImage(squareSide, squareSide, blueRgba);

    golden.data.set(toChannels(wildlyOffRgba), lastPixelIndex * channelsPerPixel);

    expect(compareImagesWithTolerance(rendered, golden, oneOffOnePixelBudget)).toBeNull();
  });

  it("reports the pixel count and the worst pixel once the budget is exceeded", () => {
    const rendered = buildImage(squareSide, squareSide, blueRgba);
    const golden = buildImage(squareSide, squareSide, blueRgba);

    golden.data.set(toChannels(wildlyOffRgba), secondPixelIndex * channelsPerPixel);
    golden.data.set(toChannels(redRgba), lastPixelIndex * channelsPerPixel);

    expect(compareImagesWithTolerance(rendered, golden, oneOffOnePixelBudget)).toBe(
      "2 pixels differ by more than 1 per channel, the budget is 1; the worst is pixel (1, 0) rendered as " +
        "rgba(51, 102, 204, 255), golden has rgba(255, 0, 0, 255)",
    );
  });
});

describe("compareImagesWithTolerance truncated images", () => {
  it("treats channels missing from a truncated render as absent colour", () => {
    const truncated = {
      data: Uint8Array.from(toChannels(blueRgba).slice(truncationStart, truncatedChannelCount)),
      height: squareSide,
      width: squareSide,
    };
    const golden = buildImage(squareSide, squareSide, blueRgba);

    expect(compareImagesWithTolerance(truncated, golden, oneOffOnePixelBudget)).toBe(
      "4 pixels differ by more than 1 per channel, the budget is 1; the worst is pixel (0, 0) rendered as " +
        "rgba(51), golden has rgba(51, 102, 204, 255)",
    );
  });

  it("treats channels missing from a truncated golden as absent colour", () => {
    const rendered = buildImage(squareSide, squareSide, blueRgba);
    const truncatedGolden = {
      data: Uint8Array.from(toChannels(blueRgba).slice(truncationStart, truncatedChannelCount)),
      height: squareSide,
      width: squareSide,
    };

    expect(compareImagesWithTolerance(rendered, truncatedGolden, oneOffOnePixelBudget)).toBe(
      "4 pixels differ by more than 1 per channel, the budget is 1; the worst is pixel (0, 0) rendered as " +
        "rgba(51, 102, 204, 255), golden has rgba(51)",
    );
  });
});
