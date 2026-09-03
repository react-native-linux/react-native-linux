import { compareDigests, computeDigests, parseUpstreamLock, serializeUpstreamLock } from "./upstream-lock.ts";
import { describe, expect, it } from "vitest";

const ORIGIN = "packages/widget/upstream.lock.json";
const NOT_A_STRING = 1;

const VALID_LOCK = {
  repo: "https://github.com/example/widget.git",
  sha256: { "src/index.ts": "abc" },
  sparsePaths: ["src"],
  tag: "v1.0.0",
};

const parse = (lock: unknown): unknown => parseUpstreamLock(JSON.stringify(lock), ORIGIN);

describe("parseUpstreamLock", () => {
  it("returns the four locked fields of a well-formed lock", () => {
    expect(parse(VALID_LOCK)).toStrictEqual(VALID_LOCK);
  });

  it("rejects a lock that is not a JSON object", () => {
    expect(() => parse("text")).toThrow(`${ORIGIN} must contain a JSON object`);
    expect(() => parse(null)).toThrow(`${ORIGIN} must contain a JSON object`);
    expect(() => parse(["src"])).toThrow(`${ORIGIN} must contain a JSON object`);
  });

  it("rejects a lock without a string repo or tag", () => {
    expect(() => parse({ ...VALID_LOCK, repo: NOT_A_STRING })).toThrow('must declare a string "repo"');
    expect(() => parse({ ...VALID_LOCK, tag: NOT_A_STRING })).toThrow('must declare a string "repo"');
  });

  it("rejects sparsePaths that are missing, not strings, or empty", () => {
    expect(() => parse({ ...VALID_LOCK, sparsePaths: "src" })).toThrow('non-empty string array "sparsePaths"');
    expect(() => parse({ ...VALID_LOCK, sparsePaths: [NOT_A_STRING] })).toThrow('non-empty string array "sparsePaths"');
    expect(() => parse({ ...VALID_LOCK, sparsePaths: [] })).toThrow('non-empty string array "sparsePaths"');
  });

  it("rejects a sha256 map that is not an object of strings", () => {
    expect(() => parse({ ...VALID_LOCK, sha256: "abc" })).toThrow('must declare an object "sha256"');
    expect(() => parse({ ...VALID_LOCK, sha256: { "src/index.ts": NOT_A_STRING } })).toThrow(
      'must declare an object "sha256"',
    );
  });
});

describe("serializeUpstreamLock", () => {
  it("writes the lock fields in a stable order and ends with a newline", () => {
    expect(serializeUpstreamLock(VALID_LOCK)).toBe(
      `{\n  "repo": "https://github.com/example/widget.git",\n  "sha256": {\n    "src/index.ts": "abc"\n  },\n  "sparsePaths": [\n    "src"\n  ],\n  "tag": "v1.0.0"\n}\n`,
    );
  });
});

describe("computeDigests", () => {
  it("hashes every listed file under its relative path", () => {
    const environment = {
      hashFile: (filePath: string): string => `hash:${filePath}`,
      listFiles: (): readonly string[] => ["src/index.ts", "README.md"],
    };

    expect(computeDigests("/tree", environment)).toStrictEqual({
      "README.md": "hash:/tree/README.md",
      "src/index.ts": "hash:/tree/src/index.ts",
    });
  });
});

describe("compareDigests", () => {
  it("reports added, removed and changed paths in sorted order and ignores equal ones", () => {
    const differences = compareDigests(
      { "a.ts": "one", "b.ts": "two", "c.ts": "three" },
      { "a.ts": "one", "b.ts": "changed", "d.ts": "four" },
    );

    expect(differences).toStrictEqual([
      { kind: "changed", path: "b.ts" },
      { kind: "removed", path: "c.ts" },
      { kind: "added", path: "d.ts" },
    ]);
  });
});
