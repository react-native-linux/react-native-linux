const POINTER_COMMAND_NAMES = new Set(["move", "click"]);
const TOKEN_SEPARATOR = " ";
const NOT_FOUND_INDEX = -1;
const STRING_START_INDEX = 0;
const AFTER_SEPARATOR_OFFSET = 1;

/**
 * `WindowDecorations::roundedRatio` with `multiply=true` (`packages/core/src/WindowDecorations.cpp`, landed in
 * #374) is `llround(value * scale)` — round half away from zero. `InputInjector.cpp`'s `move` and `click`
 * commands take surface pixels (`"move needs an x and a y in output pixels"`), while a scenario's `steps` are
 * authored in logical pixels, so the harness has to cross the identical rule before an injected coordinate
 * reaches `rnl_inject`, or a divergent rounding rule clicks a different pixel instead of failing loudly.
 * `Math.round` alone is round-half-up, which agrees with round-half-away-from-zero for every value this function
 * is ever called with, because a pointer coordinate is never negative.
 */
const logicalToSurfaceCoordinate = (logicalCoordinate: number, scale: number): number =>
  Math.round(logicalCoordinate * scale);

/**
 * `rnl_window`'s surface scale, for every scenario `scripts/e2e.ts` drives. `1` is `DimensionsSource`'s own
 * default (`packages/core/src/DimensionsSource.h`): neither `wp_fractional_scale_v1` nor
 * `wl_surface.preferred_buffer_scale` is bound yet (#51). This constant is the one call site that changes on the
 * day one of them is, rather than a second redesign of the conversion below.
 */
const SURFACE_SCALE = 1;

interface TokenSplit {
  readonly rest: string;
  readonly token: string;
}

/**
 * The text up to the first space and everything after it, or `null` when there is no space left to split on.
 * Reading a line this way, rather than `split` followed by indexing, keeps a missing token a real, testable
 * branch instead of one `noUncheckedIndexedAccess` would otherwise invent for an index `split` can never
 * actually leave empty.
 */
const takeToken = (text: string): TokenSplit | null => {
  const separatorIndex = text.indexOf(TOKEN_SEPARATOR);

  return separatorIndex === NOT_FOUND_INDEX
    ? null
    : {
        rest: text.slice(separatorIndex + AFTER_SEPARATOR_OFFSET),
        token: text.slice(STRING_START_INDEX, separatorIndex),
      };
};

/**
 * Rewrites one `rnl_inject` script line's coordinates from logical pixels to surface pixels, leaving every other
 * command — `button`, `wheel`, `key`, `type`, `sleep`, a comment, blank line — untouched, since only `move` and
 * `click` name a point.
 */
const scalePointerLine = (line: string, scale: number): string => {
  const commandSplit = takeToken(line);

  if (commandSplit === null || !POINTER_COMMAND_NAMES.has(commandSplit.token)) {
    return line;
  }

  const xSplit = takeToken(commandSplit.rest);

  if (xSplit === null) {
    return line;
  }

  const surfaceX = logicalToSurfaceCoordinate(Number(xSplit.token), scale);
  const surfaceY = logicalToSurfaceCoordinate(Number(xSplit.rest), scale);

  return [commandSplit.token, String(surfaceX), String(surfaceY)].join(TOKEN_SEPARATOR);
};

/** Converts every `move`/`click` line of a scenario's `steps` from logical pixels to `scale`'s surface pixels. */
const scalePointerSteps = (steps: readonly string[], scale: number = SURFACE_SCALE): readonly string[] =>
  steps.map((step) => scalePointerLine(step, scale));

export { SURFACE_SCALE, logicalToSurfaceCoordinate, scalePointerSteps };
