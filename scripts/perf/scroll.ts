/*
 * The feed-scroll workload both apps get: a fixed number of wheel notches at a fixed rate, injected by rnl_inject
 * through cage's zwlr_virtual_pointer, up from the bottom of the feed and back down. Each app turns a notch into
 * its own distance, so the distance is measured and checked rather than assumed.
 */

const SCROLL_NOTCHES = 16;
const NOTCH_INTERVAL_MS = 60;
/**
 * The virtual pointer outlives Chromium's wheel animation: when rnl_inject exits, the seat loses its pointer, and a
 * pointer that leaves while the feed is still animating crashed T3 Code's renderer in two of six probe runs.
 */
const POINTER_HOLD_MS = 400;
/** A notch moves the feed by a fraction of a pixel only when something rounds; less than this is a missed notch. */
const POSITION_TOLERANCE_PIXELS = 1;
const SINGLE_NOTCH = 1;

type WheelDirection = "down" | "up";

interface ScreenPoint {
  readonly xPixels: number;
  readonly yPixels: number;
}

/** The feed's `scrollTop` at the bottom before the gesture, at its highest, and after coming back down. */
interface ScrollPositions {
  readonly bottom: number;
  readonly end: number;
  readonly start: number;
  readonly top: number;
}

/** The rnl_inject script for one direction: the pointer over the feed, one notch per interval, then the hold. */
const buildWheelSteps = (point: ScreenPoint, direction: WheelDirection): readonly string[] => [
  `move ${String(Math.round(point.xPixels))} ${String(Math.round(point.yPixels))}`,
  ...Array.from({ length: SCROLL_NOTCHES }, () => [
    `wheel ${direction} ${String(SINGLE_NOTCH)}`,
    `sleep ${String(NOTCH_INTERVAL_MS)}`,
  ]).flat(),
  `sleep ${String(POINTER_HOLD_MS)}`,
];

const isNear = (left: number, right: number): boolean => Math.abs(left - right) <= POSITION_TOLERANCE_PIXELS;

/**
 * The distance travelled each way, or why the run does not count: it has to start and end at the bottom, and stop
 * short of the top, because a notch that hits the top moves nothing and would leave idle frames in the phase.
 */
const checkScrollTravel = (positions: ScrollPositions): number | string => {
  if (!isNear(positions.start, positions.bottom) || !isNear(positions.end, positions.bottom)) {
    return `the feed did not start and end at its bottom (${JSON.stringify(positions)})`;
  }

  if (positions.top <= POSITION_TOLERANCE_PIXELS || isNear(positions.top, positions.start)) {
    return `the wheel notches did not move the feed within its range (${JSON.stringify(positions)})`;
  }

  return positions.start - positions.top;
};

const formatScrollWorkload = (travelPixels: number): string =>
  `Feed scroll: ${String(SCROLL_NOTCHES)} wheel notches up, then ${String(SCROLL_NOTCHES)} down, one every ` +
  `${String(NOTCH_INTERVAL_MS)} ms, the pointer held ${String(POINTER_HOLD_MS)} ms after the last, ` +
  `${String(Math.round(travelPixels))} px each way.`;

export { buildWheelSteps, checkScrollTravel, formatScrollWorkload };
