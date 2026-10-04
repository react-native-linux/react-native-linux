interface LinuxPlatformColor {
  readonly linuxPlatformColorNames: readonly string[];
}

type ResolvePlatformColor = (name: string) => number | null;

const isLinuxPlatformColor = (color: unknown): color is LinuxPlatformColor =>
  typeof color === "object" && color !== null && "linuxPlatformColorNames" in color;

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
 * value, so an unchanged `PlatformColor` is not resolved again after a scheme change; #260 owns that.
 */
const PlatformColor = (...names: readonly string[]): LinuxPlatformColor => ({ linuxPlatformColorNames: names });

const normalizeColorObject = (color: unknown): LinuxPlatformColor | null =>
  isLinuxPlatformColor(color) ? color : null;

/**
 * The first name `__rnlPlatformColor` knows, as the ARGB integer the native colour parser reads. A name it does
 * not know is not a colour to draw in silence: it throws naming every name the call site tried.
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

  throw new Error(
    `PlatformColor(${color.linuxPlatformColorNames.map((name) => `'${name}'`).join(", ")}): none of these is a Linux platform color`,
  );
};

export { normalizeColorObject, PlatformColor, processColorObject };
