import { describe, expect, it } from "vitest";

import { compareImages } from "./png-diff.ts";

const squareSide = 2;
const narrowWidth = 1;
const shortHeight = 1;
const lastPixelIndex = 3;
const channelsPerPixel = 4;
const hexadecimalRadix = 16;
const channelPattern = /[\dA-F]{2}/gu;

const blueRgba = "3366CCFF";
const redRgba = "CC3333FF";

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
