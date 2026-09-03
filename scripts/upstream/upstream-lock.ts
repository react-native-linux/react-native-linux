import type { DigestDifference, UpstreamEnvironment, UpstreamLock } from "./upstream-types.ts";

import path from "node:path";

const JSON_INDENTATION = 2;
const NO_ENTRIES = 0;

const isRecord = (value: unknown): value is Record<string, unknown> =>
  typeof value === "object" && value !== null && !Array.isArray(value);

const isStringArray = (value: unknown): value is string[] =>
  Array.isArray(value) && value.every((entry) => typeof entry === "string");

const isStringRecord = (value: unknown): value is Record<string, string> =>
  isRecord(value) && Object.values(value).every((entry) => typeof entry === "string");

const parseLockObject = (source: string, origin: string): Record<string, unknown> => {
  const parsed: unknown = JSON.parse(source);

  if (!isRecord(parsed)) {
    throw new TypeError(`${origin} must contain a JSON object`);
  }

  return parsed;
};

const parseUpstreamLock = (source: string, origin: string): UpstreamLock => {
  const { repo, sha256, sparsePaths, tag } = parseLockObject(source, origin);

  if (typeof repo !== "string" || typeof tag !== "string") {
    throw new TypeError(`${origin} must declare a string "repo" and a string "tag"`);
  }

  if (!isStringArray(sparsePaths) || sparsePaths.length === NO_ENTRIES) {
    throw new TypeError(`${origin} must declare a non-empty string array "sparsePaths"`);
  }

  if (!isStringRecord(sha256)) {
    throw new TypeError(`${origin} must declare an object "sha256" mapping vendored paths to digests`);
  }

  return { repo, sha256, sparsePaths, tag };
};

const serializeUpstreamLock = (lock: UpstreamLock): string =>
  `${JSON.stringify({ repo: lock.repo, sha256: lock.sha256, sparsePaths: lock.sparsePaths, tag: lock.tag }, null, JSON_INDENTATION)}\n`;

const computeDigests = (
  directory: string,
  environment: Pick<UpstreamEnvironment, "hashFile" | "listFiles">,
): Record<string, string> =>
  Object.fromEntries(
    environment
      .listFiles(directory)
      .map((relativePath) => [relativePath, environment.hashFile(path.join(directory, relativePath))]),
  );

const describeDigestChange = (
  relativePath: string,
  previousDigest: string | null,
  nextDigest: string | null,
): readonly DigestDifference[] => {
  if (previousDigest === null) {
    return [{ kind: "added", path: relativePath }];
  }

  if (nextDigest === null) {
    return [{ kind: "removed", path: relativePath }];
  }

  return previousDigest === nextDigest ? [] : [{ kind: "changed", path: relativePath }];
};

const compareDigests = (
  previous: Readonly<Record<string, string>>,
  next: Readonly<Record<string, string>>,
): readonly DigestDifference[] =>
  [...new Set([...Object.keys(previous), ...Object.keys(next)])]
    .toSorted()
    .flatMap((relativePath) =>
      describeDigestChange(relativePath, previous[relativePath] ?? null, next[relativePath] ?? null),
    );

const formatDigestDifferences = (differences: readonly DigestDifference[]): readonly string[] =>
  differences.map((difference) => `  ${difference.kind} ${difference.path}`);

const countDigestDifferences = (differences: readonly DigestDifference[], kind: DigestDifference["kind"]): number =>
  differences.filter((difference) => difference.kind === kind).length;

export {
  compareDigests,
  computeDigests,
  countDigestDifferences,
  formatDigestDifferences,
  parseUpstreamLock,
  serializeUpstreamLock,
};
