import { existsSync, readFileSync, writeFileSync } from "node:fs";

import { isRecord } from "./fields.ts";
import path from "node:path";

const JSON_INDENT = 2;
const NOT_FOUND_INDEX = -1;
const ROOT_LOCATION = "the tree";

/** One serialised-tree assertion: what the window answered, and the checked-in file it has to equal. */
interface SnapshotComparison {
  readonly artifactName: string;
  readonly directory: string;
  readonly observed: unknown;
  readonly snapshot: string;
}

/**
 * Key-sorted JSON, so a snapshot compares by structure rather than by the order `folly::dynamic` happened to
 * hash its object keys into.
 */
const canonicalJson = (value: unknown): string => {
  if (Array.isArray(value)) {
    return `[${value.map((entry) => canonicalJson(entry)).join(",")}]`;
  }

  if (!isRecord(value)) {
    return JSON.stringify(value) ?? "null";
  }

  return `{${Object.keys(value)
    .toSorted()
    .map((key) => `${JSON.stringify(key)}:${canonicalJson(value[key])}`)
    .join(",")}}`;
};

const findDifferingKey = (observed: Record<string, unknown>, expected: Record<string, unknown>): string | null =>
  [...new Set([...Object.keys(observed), ...Object.keys(expected)])]
    .toSorted()
    .find((key) => canonicalJson(observed[key]) !== canonicalJson(expected[key])) ?? null;

/** The one step down a tree that first disagrees: which child, and what the two sides hold there. */
interface Divergence {
  readonly expected: unknown;
  readonly location: string;
  readonly observed: unknown;
}

const findDivergence = (observed: unknown, expected: unknown, location: string): Divergence | null => {
  if (Array.isArray(observed) && Array.isArray(expected)) {
    const index = observed.findIndex((entry, at) => canonicalJson(entry) !== canonicalJson(expected[at]));

    return index === NOT_FOUND_INDEX
      ? null
      : { expected: expected[index], location: `${location}[${String(index)}]`, observed: observed[index] };
  }

  if (isRecord(observed) && isRecord(expected)) {
    const key = findDifferingKey(observed, expected);

    return key === null ? null : { expected: expected[key], location: `${location}.${key}`, observed: observed[key] };
  }

  return null;
};

/**
 * Where two trees first disagree, named by the path to it rather than by dumping both trees: a snapshot failure
 * whose message is `the tree[0].children[1].role: "button" where the snapshot has "none"` is a finding, and one
 * that prints two hundred lines of JSON is a chore. It descends the first differing key or index and stops at
 * the first value that is not a container of one.
 */
const describeDifference = (observed: unknown, expected: unknown, location: string): string => {
  if (Array.isArray(observed) && Array.isArray(expected) && observed.length !== expected.length) {
    return `${location}: ${String(observed.length)} node(s) where the snapshot has ${String(expected.length)}`;
  }

  const divergence = findDivergence(observed, expected, location);

  return divergence === null
    ? `${location}: ${canonicalJson(observed)} where the snapshot has ${canonicalJson(expected)}`
    : describeDifference(divergence.observed, divergence.expected, divergence.location);
};

/**
 * Writes what the window answered beside the run and compares it against the checked-in file, which is what
 * makes a snapshot blessable: the artifact is already the exact bytes the snapshot has to become. A snapshot
 * that does not exist yet is reported as such rather than as a mismatch, because there is nothing to diff.
 */
const compareSnapshot = (artifactsDirectory: string, comparison: SnapshotComparison): readonly string[] => {
  const observedPath = path.join(artifactsDirectory, comparison.artifactName);

  writeFileSync(observedPath, `${JSON.stringify(comparison.observed, null, JSON_INDENT)}\n`);

  const snapshotPath = path.join(comparison.directory, comparison.snapshot);

  if (!existsSync(snapshotPath)) {
    return [`there is no snapshot at ${snapshotPath}; bless ${observedPath}`];
  }

  const expected: unknown = JSON.parse(readFileSync(snapshotPath, "utf8"));

  if (canonicalJson(comparison.observed) === canonicalJson(expected)) {
    return [];
  }

  return [
    `${comparison.snapshot} does not match: ${describeDifference(comparison.observed, expected, ROOT_LOCATION)}; ` +
      `see ${observedPath}`,
  ];
};

export { canonicalJson, compareSnapshot, describeDifference };
