import normalizeColor from "@react-native/normalize-colors";

interface LinuxPlatformColor {
  readonly linuxPlatformColorNames: readonly string[];
  readonly fallback?: string;
}

type ResolvePlatformColor = (name: string) => number | null;

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
const LAST_ARGUMENT = -1;

/**
 * A CSS colour string or number as the `0xaarrggbb` C++ reads, upstream's `0xrrggbbaa` reordered, with transparent
 * black as `DEFINED_TRANSPARENT`; `null` for one it cannot read.
 */
const processColorValue = (color: unknown): number | null => {
  const normalized = typeof color === "string" || typeof color === "number" ? normalizeColor(color) : null;

  if (normalized === null) {
    return null;
  }

  const argb = (normalized % CHANNEL_RANGE) * ALPHA_PLACE + Math.floor(normalized / CHANNEL_RANGE);

  return argb === NO_COLOR ? DEFINED_TRANSPARENT : argb;
};

const isLinuxPlatformColor = (color: unknown): color is LinuxPlatformColor =>
  typeof color === "object" &&
  color !== null &&
  "linuxPlatformColorNames" in color &&
  Array.isArray(color.linuxPlatformColorNames) &&
  color.linuxPlatformColorNames.every((name: unknown) => typeof name === "string");

const hostResolver = (): ResolvePlatformColor | null => {
  const resolver: unknown = Reflect.get(globalThis, "__rnlPlatformColor");

  if (typeof resolver !== "function") {
    return null;
  }

  return function resolveThroughHost(name: string): number | null {
    const resolved: unknown = resolver(name);

    return typeof resolved === "number" ? resolved : null;
  };
};

/**
 * #505: `PlatformColor('labelColor', 'secondaryLabelColor')` is an opaque value naming colours, resolved against
 * the current colour scheme when a prop is sent to the native side. React Native's prop diff skips a deep-equal
 * value, so an unchanged `PlatformColor` is not resolved again after a scheme change; #260 owns that. A trailing
 * `{fallback}` object, as in 0.88's `parsePlatformColorArgs`, names the colour to use when no name resolves.
 */
const PlatformColor = (...names: readonly unknown[]): LinuxPlatformColor => {
  const last = names.at(LAST_ARGUMENT);
  const linuxPlatformColorNames = names.filter((name) => typeof name === "string");

  return typeof last === "object" && last !== null && "fallback" in last && typeof last.fallback === "string"
    ? { fallback: last.fallback, linuxPlatformColorNames }
    : { linuxPlatformColorNames };
};

const normalizeColorObject = (color: unknown): LinuxPlatformColor | null =>
  isLinuxPlatformColor(color) ? color : null;

/**
 * The first name `__rnlPlatformColor` knows, as the ARGB integer the native colour parser reads, else the
 * `{fallback}` colour. A name it does not know is not a colour to draw in silence: without a fallback it throws
 * naming every name the call site tried.
 */
const processColorObject = (color: unknown, resolve = hostResolver()): number | null => {
  if (!isLinuxPlatformColor(color)) {
    return null;
  }

  for (const name of color.linuxPlatformColorNames) {
    const resolved = resolve?.(name) ?? null;

    if (resolved !== null) {
      return resolved;
    }
  }

  if ("fallback" in color) {
    return processColorValue(color.fallback);
  }

  throw new Error(
    `PlatformColor(${color.linuxPlatformColorNames.map((name) => `'${name}'`).join(", ")}): none of these is a Linux platform color`,
  );
};

export { normalizeColorObject, PlatformColor, processColorObject, processColorValue };
