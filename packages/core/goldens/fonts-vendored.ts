import { existsSync, readFileSync } from "node:fs";
import path from "node:path";

const VENDOR_COMMAND = "pnpm --filter @react-native-linux/core vendor:fonts";

const isRecord = (value: unknown): value is Record<string, unknown> =>
  typeof value === "object" && value !== null && !Array.isArray(value);

const buildMissingFileMessage = (filePath: string): string =>
  `${filePath} is missing. Run "${VENDOR_COMMAND}" and retry.`;

const buildStampMismatchMessage = (stampFilePath: string, lockFilePath: string): string =>
  `${stampFilePath} does not match ${lockFilePath}. Run "${VENDOR_COMMAND}" and retry.`;

/**
 * Deep-sorts every object's keys, so two JSON documents describing the same font pins compare equal by
 * `JSON.stringify` regardless of which order each was written in — `scripts/fonts.lock.json` and the
 * `.vendor-stamp.json` `scripts/vendor-fonts.ts` writes from it are not guaranteed to agree on key order.
 */
const canonicalize = (value: unknown): unknown => {
  if (Array.isArray(value)) {
    return value.map((entry) => canonicalize(entry));
  }

  if (isRecord(value)) {
    return Object.fromEntries(
      Object.keys(value)
        .toSorted()
        .map((key) => [key, canonicalize(value[key])]),
    );
  }

  return value;
};

/**
 * The `name` of every file `scripts/fonts.lock.json` pins, across every source family.
 *
 * Reading only what this check needs, rather than the full validating parser `scripts/vendor-fonts.ts` uses:
 * `packages/core/goldens` cannot import across the package boundary into `scripts/` (`import/no-relative-parent-imports`),
 * and the lock file is a reviewed, source-controlled artifact — a malformed one is a build-time surprise either way.
 */
const readSourceFileNames = (source: unknown, lockFilePath: string): readonly string[] => {
  if (!isRecord(source) || !Array.isArray(source["files"])) {
    throw new Error(`${lockFilePath} must declare every "sources" entry with a "files" array`);
  }

  return source["files"].map((file: unknown) => {
    if (!isRecord(file) || typeof file["name"] !== "string") {
      throw new Error(`${lockFilePath} must declare every "files" entry with a string "name"`);
    }

    return file["name"];
  });
};

const readPinnedFileNames = (lockFilePath: string): readonly string[] => {
  const parsed: unknown = JSON.parse(readFileSync(lockFilePath, "utf8"));

  if (!isRecord(parsed) || !Array.isArray(parsed["sources"])) {
    throw new Error(`${lockFilePath} must contain a JSON object with a "sources" array`);
  }

  return parsed["sources"].flatMap((source: unknown) => readSourceFileNames(source, lockFilePath));
};

const checkEveryPinnedFileExists = (fileNames: readonly string[], vendorDirectory: string): void => {
  for (const fileName of fileNames) {
    const filePath = path.join(vendorDirectory, fileName);

    if (!existsSync(filePath)) {
      throw new Error(buildMissingFileMessage(filePath));
    }
  }
};

const checkStampMatchesLock = (lockFilePath: string, vendorDirectory: string): void => {
  const stampFilePath = path.join(vendorDirectory, ".vendor-stamp.json");

  if (!existsSync(stampFilePath)) {
    throw new Error(buildMissingFileMessage(stampFilePath));
  }

  const lock = canonicalize(JSON.parse(readFileSync(lockFilePath, "utf8")));
  const stamp = canonicalize(JSON.parse(readFileSync(stampFilePath, "utf8")));

  if (JSON.stringify(lock) !== JSON.stringify(stamp)) {
    throw new Error(buildStampMismatchMessage(stampFilePath, lockFilePath));
  }
};

/**
 * Fails loudly, rather than silently rendering with whatever fontconfig substitutes, when `packages/core/fonts`
 * does not hold every face `scripts/fonts.lock.json` pins.
 *
 * #307: a stale vendor directory — its `.vendor-stamp.json` predated the lock's `Noto Color Emoji` entry — made
 * `SkFontMgr_New_Custom_Directory` answer nothing for that family, so the run fell through to fontconfig's
 * system emoji face (a different file by sha256) with no error anywhere, and the golden drifted by hundreds of
 * pixels. This is the TS half of #314's guard against that; `PinnedFontFamilies.cpp` is the C++ half, checked
 * from inside the pipeline the golden binary itself runs.
 *
 * Throws rather than returning a boolean: a golden run against the wrong fonts is not a state golden.spec.ts
 * should render a picture for and compare, so the caller is meant to let this propagate instead of catching it.
 */
const checkFontsAreVendored = (lockFilePath: string, vendorDirectory: string): void => {
  checkEveryPinnedFileExists(readPinnedFileNames(lockFilePath), vendorDirectory);
  checkStampMatchesLock(lockFilePath, vendorDirectory);
};

export { checkFontsAreVendored };
