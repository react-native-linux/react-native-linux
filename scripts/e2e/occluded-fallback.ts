/**
 * `rnl_window --frame-log` writes one of these each time the activity its fallback interval stretches by changes, so
 * two consecutive records always name different activities. `fallbackNs` is diagnostic only: on a headless output the
 * period it stretches is a moving median of presents, and a thermal cap equalizes it, so only the activities are
 * graded (#770).
 */
const FALLBACK_MARKER = '"fallbackNs":';
const FALLBACK_PATTERN = /^\{"fallbackNs":\d+,"activity":"(?<activity>active|inactive|occluded)"\}$/u;
const OCCLUDED = "occluded";
const FIRST_INDEX = 0;
const LAST_OFFSET = 1;

/**
 * The e2e half of #770: the window's fallback is occluded while the compositor suspends it and is not once it resumes,
 * so an occluded record sits between two others. A record it cannot read fails the check rather than being skipped.
 * `false` asks nothing.
 */
const findOccludedFallbackFailures = (frameLogText: string, isRequired: boolean): readonly string[] => {
  if (!isRequired) {
    return [];
  }

  const lines = frameLogText.split("\n").filter((line) => line.includes(FALLBACK_MARKER));
  const activities = lines.map((line) => FALLBACK_PATTERN.exec(line)?.groups?.["activity"] ?? null);

  if (activities.includes(null)) {
    return ["the frame log carries a fallback record it cannot read"];
  }

  const occludedIndex = activities.indexOf(OCCLUDED);

  return occludedIndex > FIRST_INDEX && occludedIndex < activities.length - LAST_OFFSET
    ? []
    : [`the fallback must go from another activity to occluded and back, the log records ${activities.join(" → ")}`];
};

export { findOccludedFallbackFailures };
