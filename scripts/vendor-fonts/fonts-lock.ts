interface FontFileLock {
  readonly name: string;
  readonly path: string;
  readonly sha256: string;
}

interface FontSourceLock {
  readonly baseUrl: string;
  readonly commit: string;
  readonly family: string;
  readonly files: readonly FontFileLock[];
  readonly license: string;
}

interface FontsLock {
  readonly sources: readonly FontSourceLock[];
}

const isRecord = (value: unknown): value is Record<string, unknown> =>
  typeof value === "object" && value !== null && !Array.isArray(value);

const parseFontFile = (value: unknown, origin: string): FontFileLock => {
  if (
    !isRecord(value) ||
    typeof value.name !== "string" ||
    typeof value.path !== "string" ||
    typeof value.sha256 !== "string"
  ) {
    throw new Error(`${origin} must declare every "files" entry with string "name", "path" and "sha256"`);
  }

  return { name: value.name, path: value.path, sha256: value.sha256 };
};

const parseFontSource = (value: unknown, origin: string): FontSourceLock => {
  if (
    !isRecord(value) ||
    typeof value.baseUrl !== "string" ||
    typeof value.commit !== "string" ||
    typeof value.family !== "string" ||
    typeof value.license !== "string" ||
    !Array.isArray(value.files)
  ) {
    throw new Error(
      `${origin} must declare every "sources" entry with string "baseUrl", "commit", "family", "license" and "files"`,
    );
  }

  return {
    baseUrl: value.baseUrl,
    commit: value.commit,
    family: value.family,
    files: value.files.map((entry) => parseFontFile(entry, origin)),
    license: value.license,
  };
};

/**
 * The lock file is the artifact under review, so anything it cannot mean is an error rather than a default.
 */
const parseFontsLock = (source: string, origin: string): FontsLock => {
  const parsed: unknown = JSON.parse(source);

  if (!isRecord(parsed) || !Array.isArray(parsed.sources)) {
    throw new Error(`${origin} must contain a JSON object with a "sources" array`);
  }

  return { sources: parsed.sources.map((entry) => parseFontSource(entry, origin)) };
};

/**
 * The stamp is a cache of what was last written into the vendor directory, not an artifact anybody reviews, so a
 * stamp this version cannot read is a cache miss and never an error. That includes the flat single-family stamp
 * written before the lock file grew its `sources` list: a checkout holding one has to be able to refresh to the
 * faces the current lock names, and throwing on it would strand that checkout on the old fonts forever.
 */
const parseFontsStamp = (source: string): FontsLock | null => {
  try {
    return parseFontsLock(source, "the vendor stamp");
  } catch {
    return null;
  }
};

/**
 * Whether the vendor directory already holds exactly what the lock asks for.
 *
 * The comparison is the whole parsed lock, not each file's upstream path and digest: the same bytes written under
 * a different `name` is a different vendor directory, and a stamp identity that ignored `name` would skip the
 * download and leave the renamed file absent. Both sides come from `parseFontsLock`, which builds its objects
 * with one fixed key order, so serialising them is a stable comparison rather than a hash of formatting.
 */
const isStampCurrent = (lock: FontsLock, stamp: FontsLock | null): boolean =>
  stamp !== null && JSON.stringify(stamp) === JSON.stringify(lock);

export { isStampCurrent, parseFontsLock, parseFontsStamp };
