import { readNonNegativeInteger, readObject, readPositiveInteger, readPositiveNumber } from "./fields.ts";

/**
 * The perf gate of #7: `p95Ms`/`minFrames` bound the p95 `wp_presentation` frame time and the frames needed for
 * it to mean anything; omitted opts out, which a scenario whose idle window presents only its few work frames does
 * (#703). `maxHangs` (#345) caps hang-thresholded frames; `null` opts out, `0` is a measured zero.
 * `maxJournalledFrames` (#42) caps the frames that painted damage, which is how a scenario says nothing repaints
 * once its input stops; `null` opts out. `p50Ms` (#20) bounds the median, which a client slower than the headless
 * output's frame timer moves while its p95 can stay inside `p95Ms`; omitted opts out.
 */
interface FrameBudget {
  readonly maxHangs: number | null;
  readonly maxJournalledFrames: number | null;
  readonly minFrames?: number;
  readonly p50Ms?: number;
  readonly p95Ms?: number;
}

const readFrameBudget = (record: Record<string, unknown>, sourceName: string): FrameBudget | null => {
  if (!("frameBudget" in record)) {
    return null;
  }

  const budget = readObject(record["frameBudget"], "frameBudget", sourceName);

  return {
    maxHangs:
      "maxHangs" in budget ? readNonNegativeInteger(budget["maxHangs"], "frameBudget.maxHangs", sourceName) : null,
    maxJournalledFrames:
      "maxJournalledFrames" in budget
        ? readNonNegativeInteger(budget["maxJournalledFrames"], "frameBudget.maxJournalledFrames", sourceName)
        : null,
    ...("minFrames" in budget
      ? { minFrames: readPositiveInteger(budget["minFrames"], "frameBudget.minFrames", sourceName) }
      : {}),
    ...("p95Ms" in budget ? { p95Ms: readPositiveNumber(budget["p95Ms"], "frameBudget.p95Ms", sourceName) } : {}),
    ...("p50Ms" in budget ? { p50Ms: readPositiveNumber(budget["p50Ms"], "frameBudget.p50Ms", sourceName) } : {}),
  };
};

export { readFrameBudget };
export type { FrameBudget };
