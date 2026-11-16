/**
 * `rnl_window --frame-log` writes one of these each time the activity its fallback interval stretches by changes, so
 * two consecutive records always name different activities.
 */
const FALLBACK_MARKER = '"fallbackNs":';
const FALLBACK_PATTERN =
  /^\{"fallbackNs":(?<interval>\d+),"activity":"(?<activity>active|inactive|occluded)","atNs":(?<at>\d+)\}$/u;
const OCCLUDED = "occluded";
const FIRST_INDEX = 0;
const ADJACENT = 1;
const HALF = 2;
const NANOSECONDS_PER_MILLISECOND = 1_000_000;

interface FallbackRecord {
  readonly activity: string;
  readonly atNanoseconds: number;
  readonly intervalNanoseconds: number;
}

const readFallbackRecord = (line: string): FallbackRecord | null => {
  const groups = FALLBACK_PATTERN.exec(line)?.groups;

  return groups
    ? {
        activity: String(groups["activity"]),
        atNanoseconds: Number(groups["at"]),
        intervalNanoseconds: Number(groups["interval"]),
      }
    : null;
};

const toMilliseconds = (nanoseconds: number): string => String(Math.round(nanoseconds / NANOSECONDS_PER_MILLISECOND));

/**
 * A resume ends the occluded wait at once (#770), so the record after the occluded one comes well inside that wait:
 * under half of it, which leaves room for the scenario's step interval while the wait is long.
 */
const findLateResume = (occluded: FallbackRecord, resumed: FallbackRecord): readonly string[] => {
  const waitedNanoseconds = resumed.atNanoseconds - occluded.atNanoseconds;

  return waitedNanoseconds < occluded.intervalNanoseconds / HALF
    ? []
    : [
        `the window resumed ${toMilliseconds(waitedNanoseconds)} ms after it went occluded, ` +
          `not inside half of its ${toMilliseconds(occluded.intervalNanoseconds)} ms occluded wait`,
      ];
};

/**
 * The e2e half of #770: the fallback goes occluded while the compositor suspends the window and returns as soon as it
 * resumes, so an occluded record sits between two others and the one after it comes early. A record it cannot read
 * fails the check rather than being skipped. `false` asks nothing.
 */
const findOccludedFallbackFailures = (frameLogText: string, isRequired: boolean): readonly string[] => {
  if (!isRequired) {
    return [];
  }

  const lines = frameLogText.split("\n").filter((line) => line.includes(FALLBACK_MARKER));
  const records = lines.map((line) => readFallbackRecord(line)).filter((record) => record !== null);

  if (records.length !== lines.length) {
    return ["the frame log carries a fallback record it cannot read"];
  }

  const occludedIndex = records.findIndex((record) => record.activity === OCCLUDED);
  const occluded = records[occludedIndex];
  const resumed = records[occludedIndex + ADJACENT];

  return occluded && resumed && occludedIndex > FIRST_INDEX
    ? findLateResume(occluded, resumed)
    : [
        `the fallback must go from another activity to occluded and back, the log records ${records
          .map((record) => record.activity)
          .join(" → ")}`,
      ];
};

export { findOccludedFallbackFailures };
