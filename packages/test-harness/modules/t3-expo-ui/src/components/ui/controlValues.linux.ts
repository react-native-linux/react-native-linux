export function clampNumber(value: number, min: number, max: number) {
  return Math.min(max, Math.max(min, value));
}

/** The nearest step from `min`, inside the range: what SwiftUI's stepped Slider reports. */
export function snapToStep(value: number, min: number, max: number, step: number) {
  return clampNumber(min + Math.round((value - min) / step) * step, min, max);
}

/** Where `value` sits along the range, 0 at `min` and 1 at `max`. */
export function rangeFraction(value: number, min: number, max: number) {
  return max > min ? clampNumber((value - min) / (max - min), 0, 1) : 0;
}

/** The value under a pointer `offset` pixels along a track `width` pixels wide. */
export function valueAtOffset(offset: number, width: number, min: number, max: number) {
  return width > 0 ? min + clampNumber(offset / width, 0, 1) * (max - min) : min;
}
