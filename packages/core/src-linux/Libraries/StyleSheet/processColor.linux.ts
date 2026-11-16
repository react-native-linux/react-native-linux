import { normalizeColorObject, processColorObject, processColorValue } from "./PlatformColorValueTypes.linux.ts";

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

  return processColorValue(color);
};

export default processColor;
