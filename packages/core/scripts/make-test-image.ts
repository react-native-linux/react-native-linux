import { mkdirSync, writeFileSync } from "node:fs";

import { PNG } from "pngjs";

import path from "node:path";
import { stdout } from "node:process";

const IMAGE_WIDTH = 64;
const IMAGE_HEIGHT = 48;
const HALF_WIDTH = 32;
const HALF_HEIGHT = 24;
const BORDER_WIDTH = 2;
const CENTRE_LEFT = 28;
const CENTRE_TOP = 20;
const CENTRE_SIZE = 8;
const CHANNELS_PER_PIXEL = 4;
const OPAQUE_ALPHA = 255;

/*
 * Written as hexadecimal text rather than as numbers because the formatter lowercases hexadecimal literals while
 * the linter demands uppercase digits and digit separators; a string keeps the colours in the notation the docs
 * and the other fixtures use.
 */
const HEX_CHANNEL_PATTERN = /../gu;
const HEX_RADIX = 16;
const MISSING_CHANNEL = 0;

const FRAME_COLOUR = "F2F4F8";
const TOP_LEFT_COLOUR = "E06C75";
const TOP_RIGHT_COLOUR = "98C379";
const BOTTOM_LEFT_COLOUR = "61AFEF";
const BOTTOM_RIGHT_COLOUR = "E5C07B";
const CENTRE_COLOUR = "14161A";

type Colour = readonly [number, number, number];

const channelsOf = (colour: string): Colour => {
  const [red, green, blue] = (colour.match(HEX_CHANNEL_PATTERN) ?? []).map((channel) =>
    Number.parseInt(channel, HEX_RADIX),
  );

  return [red ?? MISSING_CHANNEL, green ?? MISSING_CHANNEL, blue ?? MISSING_CHANNEL];
};

const isInsideBorder = (column: number, row: number): boolean =>
  column < BORDER_WIDTH ||
  row < BORDER_WIDTH ||
  column >= IMAGE_WIDTH - BORDER_WIDTH ||
  row >= IMAGE_HEIGHT - BORDER_WIDTH;

const isInsideCentre = (column: number, row: number): boolean =>
  column >= CENTRE_LEFT && column < CENTRE_LEFT + CENTRE_SIZE && row >= CENTRE_TOP && row < CENTRE_TOP + CENTRE_SIZE;

const quadrantColour = (column: number, row: number): string => {
  if (row < HALF_HEIGHT) {
    return column < HALF_WIDTH ? TOP_LEFT_COLOUR : TOP_RIGHT_COLOUR;
  }

  return column < HALF_WIDTH ? BOTTOM_LEFT_COLOUR : BOTTOM_RIGHT_COLOUR;
};

const pixelColour = (column: number, row: number): Colour => {
  if (isInsideBorder(column, row)) {
    return channelsOf(FRAME_COLOUR);
  }

  if (isInsideCentre(column, row)) {
    return channelsOf(CENTRE_COLOUR);
  }

  return channelsOf(quadrantColour(column, row));
};

const channelValue = (channelOffset: number): number => {
  const pixelIndex = Math.floor(channelOffset / CHANNELS_PER_PIXEL);
  const channelIndex = channelOffset % CHANNELS_PER_PIXEL;
  const column = pixelIndex % IMAGE_WIDTH;
  const row = Math.floor(pixelIndex / IMAGE_WIDTH);

  return [...pixelColour(column, row), OPAQUE_ALPHA][channelIndex] ?? OPAQUE_ALPHA;
};

/**
 * Writes the checked-in `<Image>` test asset: a 64x48 RGBA PNG with a light border, four differently coloured
 * quadrants and a dark square in the middle.
 *
 * Nothing about it is random, and every feature earns its place in the golden. The 4:3 aspect makes `cover` and
 * `contain` produce visibly different rectangles inside the fixture's 130x120 tiles, the four quadrants make the
 * anchoring of `repeat` and the centring of `center` readable at a glance, and the border makes a rounded-corner
 * clip obvious because it is the part the corners cut away.
 */
const image = new PNG({ height: IMAGE_HEIGHT, width: IMAGE_WIDTH });

image.data.set(
  Uint8Array.from({ length: IMAGE_WIDTH * IMAGE_HEIGHT * CHANNELS_PER_PIXEL }, (_channel, channelOffset) =>
    channelValue(channelOffset),
  ),
);

const assetsDirectory = path.join(import.meta.dirname, "..", "assets");
const outputPath = path.join(assetsDirectory, "rnl-test-image.png");

mkdirSync(assetsDirectory, { recursive: true });
writeFileSync(outputPath, PNG.sync.write(image));
stdout.write(`wrote ${outputPath}\n`);
