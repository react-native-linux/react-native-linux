import { describe, expect, it } from "vitest";
import { isStampCurrent, parseFontsLock, parseFontsStamp } from "./fonts-lock.ts";

type FontsLock = ReturnType<typeof parseFontsLock>;

const ORIGIN = "scripts/fonts.lock.json";
const NOT_A_STRING = 1;

const FILE = { name: "NotoSans-Regular.ttf", path: "fonts/NotoSans-Regular.ttf", sha256: "478c558" };

const SOURCE = {
  baseUrl: "https://example.invalid/notofonts",
  commit: "2ad4e55",
  family: "Noto Sans",
  files: [FILE],
  license: "SIL Open Font License 1.1",
};

const VALID_LOCK = { sources: [SOURCE] };

// A source on its own is exactly the shape scripts/fonts.lock.json had before it grew its "sources" list.
// An existing checkout's .vendor-stamp.json still holds that shape, and it has to read as a cache miss.
const LEGACY_FLAT_STAMP = SOURCE;

const parse = (lock: unknown): FontsLock => parseFontsLock(JSON.stringify(lock), ORIGIN);

const withFile = (patch: Record<string, unknown>): unknown => ({
  sources: [{ ...SOURCE, files: [{ ...FILE, ...patch }] }],
});

const withSource = (patch: Record<string, unknown>): unknown => ({ sources: [{ ...SOURCE, ...patch }] });

describe("parseFontsLock", () => {
  it("returns every source and file of a well-formed lock", () => {
    expect(parse(VALID_LOCK)).toStrictEqual(VALID_LOCK);
  });

  it("rejects a lock that is not an object with a sources array", () => {
    const message = `${ORIGIN} must contain a JSON object with a "sources" array`;

    expect(() => parse("text")).toThrow(message);
    expect(() => parse(null)).toThrow(message);
    expect(() => parse([SOURCE])).toThrow(message);
    expect(() => parse(LEGACY_FLAT_STAMP)).toThrow(message);
  });

  it("rejects a source missing any of its five fields", () => {
    for (const field of ["baseUrl", "commit", "family", "license"]) {
      expect(() => parse(withSource({ [field]: NOT_A_STRING }))).toThrow('must declare every "sources" entry');
    }

    expect(() => parse(withSource({ files: "one" }))).toThrow('must declare every "sources" entry');
    expect(() => parse({ sources: ["Noto Sans"] })).toThrow('must declare every "sources" entry');
  });

  it("rejects a file entry missing any of its three fields", () => {
    for (const field of ["name", "path", "sha256"]) {
      expect(() => parse(withFile({ [field]: NOT_A_STRING }))).toThrow('must declare every "files" entry');
    }

    expect(() => parse(withSource({ files: ["NotoSans-Regular.ttf"] }))).toThrow('must declare every "files" entry');
  });
});

describe("parseFontsStamp", () => {
  it("reads a stamp this version wrote", () => {
    expect(parseFontsStamp(JSON.stringify(VALID_LOCK))).toStrictEqual(VALID_LOCK);
  });

  it("reads a legacy flat stamp as a cache miss rather than throwing", () => {
    expect(parseFontsStamp(JSON.stringify(LEGACY_FLAT_STAMP))).toBeNull();
  });

  it("reads an unparsable stamp as a cache miss", () => {
    expect(parseFontsStamp("{")).toBeNull();
  });
});

describe("isStampCurrent", () => {
  const stamp = parse(VALID_LOCK);

  it("is false when there is no stamp at all", () => {
    expect(isStampCurrent(stamp, null)).toBe(false);
  });

  it("is true when the stamp is the lock", () => {
    expect(isStampCurrent(parse(VALID_LOCK), stamp)).toBe(true);
  });

  it("is false when only the output name changed", () => {
    const renamed = parse(withFile({ name: "NotoSans.ttf" }));

    expect(isStampCurrent(renamed, stamp)).toBe(false);
  });

  it("is false when the pinned digest or the commit changed", () => {
    const rehashed = parse(withFile({ sha256: "000000" }));
    const moved = parse(withSource({ commit: "9999999" }));

    expect(isStampCurrent(rehashed, stamp)).toBe(false);
    expect(isStampCurrent(moved, stamp)).toBe(false);
  });

  it("is false when the lock gained a source the stamp does not have", () => {
    const withEmoji = { sources: [SOURCE, { ...SOURCE, family: "Noto Color Emoji" }] };

    expect(isStampCurrent(parse(withEmoji), stamp)).toBe(false);
  });
});
