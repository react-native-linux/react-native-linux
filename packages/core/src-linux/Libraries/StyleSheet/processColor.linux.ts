import { normalizeColorObject, processColorObject } from "./PlatformColorValueTypes.linux.ts";
import normalizeColor from "@react-native/normalize-colors";

/**
 * Fully transparent black, as `0xaarrggbb` but not 0. The cxx platform's `UndefinedColor` is 0, so upstream's
 * `processColor('transparent')` reaches C++ as "no colour" and a nested `<Text color="transparent">` falls back to
 * its parent's colour (#112, react-native#53343 on Android). Alpha is still 0, so it paints nothing.
 */
const DEFINED_TRANSPARENT = 0x00_00_00_01;
const NO_COLOR = 0;
const CHANNEL_RANGE = 256;
// Where the alpha byte goes once it moves from the low byte to the high one: 256 cubed.
const ALPHA_PLACE = 16_777_216;

/**
 * React Native's `processColor` for linux: upstream's, with `0xrrggbbaa` reordered to the `0xaarrggbb` C++ reads,
 * except that transparent black crosses as `DEFINED_TRANSPARENT` rather than as the undefined colour, and that a
 * colour it cannot read, like an unset one, is `null`, which clears the prop natively exactly as upstream's
 * `undefined` does.
 */
const processColor = (color: unknown): number | null => {
  if ((color ?? null) === null) {
    return null;
  }

  const colorObject = normalizeColorObject(color);

  if (colorObject !== null) {
    return processColorObject(colorObject);
  }

  const normalized = typeof color === "string" || typeof color === "number" ? normalizeColor(color) : null;

  if (normalized === null) {
    return null;
  }

  const argb = (normalized % CHANNEL_RANGE) * ALPHA_PLACE + Math.floor(normalized / CHANNEL_RANGE);

  return argb === NO_COLOR ? DEFINED_TRANSPARENT : argb;
};

export default processColor;
