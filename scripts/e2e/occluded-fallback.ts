/**
 * `rnl_window --frame-log` writes one of these each time the activity its fallback interval stretches by changes, so
 * two consecutive records always name different activities.
 */
const FALLBACK_MARKER = '"fallbackNs":';
const FALLBACK_PATTERN = /^\{"fallbackNs":(?<nanoseconds>\d+),"activity":"(?<activity>active|inactive|occluded)"\}$/u;
const ADJACENT = 1;

interface FallbackRecord {
  readonly activity: string;
  readonly nanoseconds: number;
}

const readFallbackRecord = (line: string): FallbackRecord | null => {
  const groups = FALLBACK_PATTERN.exec(line)?.groups;

  return groups ? { activity: String(groups["activity"]), nanoseconds: Number(groups["nanoseconds"]) } : null;
};

const isStretchedWhileOccluded = (records: readonly FallbackRecord[]): boolean =>
  records.some(
    (record, index) =>
      record.activity === "occluded" &&
      (records[index - ADJACENT]?.nanoseconds ?? record.nanoseconds) < record.nanoseconds &&
      (records[index + ADJACENT]?.nanoseconds ?? record.nanoseconds) < record.nanoseconds,
  );

/**
 * The e2e half of #770: the window's fallback interval grows when the compositor suspends it and shrinks when it
 * resumes, so an occluded record's interval exceeds both the record before it and the record after it. A record it
 * cannot read fails the check rather than being skipped. `false` asks nothing.
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

  const recorded = records.map((record) => `${record.activity} ${String(record.nanoseconds)} ns`).join(", ");

  return isStretchedWhileOccluded(records)
    ? []
    : [`the fallback interval must grow while the window is occluded and shrink after it, the log records ${recorded}`];
};

export { findOccludedFallbackFailures };
