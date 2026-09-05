import { mkdirSync, writeFileSync } from "node:fs";

import path from "node:path";
import { stdout } from "node:process";

const IMAGE_WIDTH = 8;
const IMAGE_HEIGHT = 6;
const QUADRANT_WIDTH = 4;
const QUADRANT_HEIGHT = 3;
const TOP_QUADRANTS = 0;
const BOTTOM_QUADRANTS = 2;
const LEFT_QUADRANT = 0;
const RIGHT_QUADRANT = 1;
const FRAME_COUNT = 4;
const FRAME_DELAY_HUNDREDTHS = 10;

/*
 * Written as hexadecimal text rather than as numbers for the reason scripts/make-test-image.ts states: the
 * formatter lowercases hexadecimal literals while the linter demands uppercase digits.
 */
const HEX_CHANNEL_PATTERN = /../gu;
const HEX_RADIX = 16;
const BINARY_RADIX = 2;
const BITS_PER_BYTE = 8;
const BYTE_VALUES = 256;
const NO_VALUE = 0;
const FIRST_CHARACTER = 0;
const MISSING_CODE_POINT = 0;

/*
 * The same four quadrant colours packages/core/assets/rnl-test-image.png uses, so the animated fixture and the
 * static one read as one family in the goldens. Each frame rotates them by one quadrant, which is what makes the
 * frame a golden landed on nameable by looking at it.
 */
const PALETTE = ["E06C75", "98C379", "61AFEF", "E5C07B"];

const MAXIMUM_SUB_BLOCK_LENGTH = 255;

/*
 * The format's marker bytes, in decimal because the formatter lowercases hexadecimal literals while the linter
 * demands uppercase digits. In the hexadecimal the GIF89a specification writes them in, in declaration order:
 * 0x3B, 0x21, 0xF9, 4, 0xFF, 0x2C, 0, 0.
 */
const GIF_HEADER = "GIF89a";
const GIF_TRAILER = 59;
const EXTENSION_INTRODUCER = 33;
const GRAPHIC_CONTROL_LABEL = 249;
const GRAPHIC_CONTROL_BLOCK_SIZE = 4;
const APPLICATION_EXTENSION_LABEL = 255;
const IMAGE_SEPARATOR = 44;
const BLOCK_TERMINATOR = 0;
const SCREEN_ORIGIN = 0;

// 0x91: global colour table present, colour resolution 1, unsorted, table of 2^(1+1) = 4 entries.
const SCREEN_DESCRIPTOR_PACKED = 145;
// 0x04: disposal method 1, "do not dispose". Every frame here is opaque and covers the whole logical screen.
const GRAPHIC_CONTROL_PACKED = 4;
const NO_TRANSPARENT_INDEX = 0;
const NETSCAPE_IDENTIFIER = "NETSCAPE2.0";
const NETSCAPE_SUB_BLOCK_SIZE = 3;
const NETSCAPE_SUB_BLOCK_INDEX = 1;
const LOOP_FOREVER = 0;

const LZW_MINIMUM_CODE_SIZE = 2;
const LZW_CLEAR_CODE = 4;
const LZW_END_CODE = 5;
const LZW_CODE_WIDTH = 3;
/*
 * The decoder adds one dictionary entry per code after the first following a clear, so two literals per clear
 * leave its table at six and seven entries and the code width at three bits forever. That is the whole of the
 * "uncompressed GIF" technique: no dictionary is built, so no width change can be got wrong, and a fixture this
 * small loses nothing by it.
 */
const LITERALS_PER_CLEAR = 2;
const FIRST_LITERAL_AFTER_A_CLEAR = 0;

const channelsOf = (colour: string): readonly number[] =>
  (colour.match(HEX_CHANNEL_PATTERN) ?? []).map((channel) => Number.parseInt(channel, HEX_RADIX));

const littleEndianPair = (value: number): readonly number[] => [
  value % BYTE_VALUES,
  Math.floor(value / BYTE_VALUES) % BYTE_VALUES,
];

const asciiBytes = (text: string): readonly number[] =>
  [...text].map((character) => character.codePointAt(FIRST_CHARACTER) ?? MISSING_CODE_POINT);

/**
 * Which palette entry the pixel at `offset` of `frameIndex` carries: the quadrant it falls in, rotated by the
 * frame.
 */
const pixelIndexAt = (frameIndex: number, offset: number): number => {
  const column = offset % IMAGE_WIDTH;
  const row = Math.floor(offset / IMAGE_WIDTH);
  const quadrant =
    (row < QUADRANT_HEIGHT ? TOP_QUADRANTS : BOTTOM_QUADRANTS) +
    (column < QUADRANT_WIDTH ? LEFT_QUADRANT : RIGHT_QUADRANT);

  return (quadrant + frameIndex) % PALETTE.length;
};

/**
 * One code as the bits a decoder reads it out of, least significant first — which is the order GIF packs them in.
 */
const bitsOf = (code: number): string =>
  Array.from({ length: LZW_CODE_WIDTH }, (_bit, index) => Math.floor(code / BINARY_RADIX ** index) % BINARY_RADIX).join(
    "",
  );

/**
 * One byte from eight of those bits, or from however few of them are left at the end: the missing high bits of a
 * final short group are zero, which is exactly what padding the stream out means.
 */
const byteValue = (bits: string): number =>
  [...bits].reduce((value, bit, index) => value + Number(bit) * BINARY_RADIX ** index, NO_VALUE);

/**
 * The codes of one frame as bytes. Every code here is three bits wide, so this is one bit string cut into bytes.
 */
const packCodes = (codes: readonly number[]): readonly number[] => {
  const bits = codes.map((code) => bitsOf(code)).join("");
  const byteCount = Math.ceil(bits.length / BITS_PER_BYTE);

  return Array.from({ length: byteCount }, (_byte, index) => {
    const offset = index * BITS_PER_BYTE;

    return byteValue(bits.slice(offset, offset + BITS_PER_BYTE));
  });
};

const lzwCodes = (frameIndex: number): readonly number[] => [
  ...Array.from({ length: IMAGE_WIDTH * IMAGE_HEIGHT }, (_pixel, offset) =>
    offset % LITERALS_PER_CLEAR === FIRST_LITERAL_AFTER_A_CLEAR
      ? [LZW_CLEAR_CODE, pixelIndexAt(frameIndex, offset)]
      : [pixelIndexAt(frameIndex, offset)],
  ).flat(),
  LZW_END_CODE,
];

const subBlocks = (bytes: readonly number[]): readonly number[] => [
  ...Array.from({ length: Math.ceil(bytes.length / MAXIMUM_SUB_BLOCK_LENGTH) }, (_block, index) => {
    const offset = index * MAXIMUM_SUB_BLOCK_LENGTH;
    const chunk = bytes.slice(offset, offset + MAXIMUM_SUB_BLOCK_LENGTH);

    return [chunk.length, ...chunk];
  }).flat(),
  BLOCK_TERMINATOR,
];

const frameBytes = (frameIndex: number): readonly number[] => [
  EXTENSION_INTRODUCER,
  GRAPHIC_CONTROL_LABEL,
  GRAPHIC_CONTROL_BLOCK_SIZE,
  GRAPHIC_CONTROL_PACKED,
  ...littleEndianPair(FRAME_DELAY_HUNDREDTHS),
  NO_TRANSPARENT_INDEX,
  BLOCK_TERMINATOR,
  IMAGE_SEPARATOR,
  ...littleEndianPair(SCREEN_ORIGIN),
  ...littleEndianPair(SCREEN_ORIGIN),
  ...littleEndianPair(IMAGE_WIDTH),
  ...littleEndianPair(IMAGE_HEIGHT),
  BLOCK_TERMINATOR,
  LZW_MINIMUM_CODE_SIZE,
  ...subBlocks(packCodes(lzwCodes(frameIndex))),
];

/**
 * Writes the checked-in animated `<Image>` test asset: an 8x6 GIF89a of four frames, each a hundred milliseconds
 * long, looping forever, whose four quadrants rotate through one palette.
 *
 * Nothing about it is random. The rotation makes the frame a run stopped on readable from the picture alone,
 * which is what an animated golden has to prove; the 4:3 aspect makes `cover` and `contain` place it differently
 * in the fixture's 130x120 tiles, exactly as the static asset's does; and four frames at 100 ms put a whole loop
 * inside the 400 ms a headless run reaches in twenty-four frames at 60 Hz. The pixels are written as literal LZW
 * codes separated by clear codes rather than compressed, so the encoder has no dictionary to get wrong.
 */
const bytes: readonly number[] = [
  ...asciiBytes(GIF_HEADER),
  ...littleEndianPair(IMAGE_WIDTH),
  ...littleEndianPair(IMAGE_HEIGHT),
  SCREEN_DESCRIPTOR_PACKED,
  BLOCK_TERMINATOR,
  BLOCK_TERMINATOR,
  ...PALETTE.flatMap((colour) => channelsOf(colour)),
  EXTENSION_INTRODUCER,
  APPLICATION_EXTENSION_LABEL,
  NETSCAPE_IDENTIFIER.length,
  ...asciiBytes(NETSCAPE_IDENTIFIER),
  NETSCAPE_SUB_BLOCK_SIZE,
  NETSCAPE_SUB_BLOCK_INDEX,
  ...littleEndianPair(LOOP_FOREVER),
  BLOCK_TERMINATOR,
  ...Array.from({ length: FRAME_COUNT }, (_frame, frameIndex) => frameBytes(frameIndex)).flat(),
  GIF_TRAILER,
];

const assetsDirectory = path.join(import.meta.dirname, "..", "assets");
const outputPath = path.join(assetsDirectory, "rnl-test-animation.gif");

mkdirSync(assetsDirectory, { recursive: true });
writeFileSync(outputPath, Uint8Array.from(bytes));
stdout.write(`wrote ${outputPath}\n`);
