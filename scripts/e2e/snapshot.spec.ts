import { afterEach, beforeEach, describe, expect, it } from "vitest";
import { canonicalJson, compareSnapshot, describeDifference } from "./snapshot.ts";
import { mkdtempSync, rmSync, writeFileSync } from "node:fs";

import path from "node:path";
import { tmpdir } from "node:os";

const FIRST_FAILURE = 0;

describe("canonicalJson", () => {
  it("compares equal for objects whose keys arrived in a different order", () => {
    // Built from pairs because the source order is the point and the linter sorts every literal it sees.
    const inHashOrder = Object.fromEntries([
      ["outer", "c"],
      [
        "inner",
        Object.fromEntries([
          ["second", "b"],
          ["first", "a"],
        ]),
      ],
    ]);

    expect(canonicalJson(inHashOrder)).toBe(canonicalJson({ inner: { first: "a", second: "b" }, outer: "c" }));
  });

  it("keeps the order of an array, which is meaning rather than hashing", () => {
    expect(canonicalJson(["second", "first"])).not.toBe(canonicalJson(["first", "second"]));
  });

  it("keeps a value JSON cannot represent as null", () => {
    expect(canonicalJson(Symbol.iterator)).toBe("null");
  });
});

describe("describeDifference", () => {
  it("names the path to the first differing value", () => {
    expect(
      describeDifference([{ children: [{ role: "button" }] }], [{ children: [{ role: "none" }] }], "the tree"),
    ).toBe('the tree[0].children[0].role: "button" where the snapshot has "none"');
  });

  it("names a key one side has and the other does not", () => {
    expect(describeDifference([{ name: "Send" }], [{}], "the tree")).toBe(
      'the tree[0].name: "Send" where the snapshot has null',
    );
  });

  it("counts the nodes of a list whose length changed", () => {
    expect(describeDifference([{ tag: 3 }, { tag: 4 }], [{ tag: 3 }], "the tree")).toBe(
      "the tree: 2 node(s) where the snapshot has 1",
    );
  });

  it("compares two equal lists whole, which only a caller that did not compare first asks for", () => {
    expect(describeDifference([{ tag: 3 }], [{ tag: 3 }], "the tree")).toBe(
      'the tree: [{"tag":3}] where the snapshot has [{"tag":3}]',
    );
  });

  it("compares two equal objects whole for the same reason", () => {
    expect(describeDifference({ tag: 3 }, { tag: 3 }, "the tree")).toBe(
      'the tree: {"tag":3} where the snapshot has {"tag":3}',
    );
  });

  it("compares two values of different shapes whole", () => {
    expect(describeDifference("button", ["button"], "the tree")).toBe(
      'the tree: "button" where the snapshot has ["button"]',
    );
  });
});

describe("compareSnapshot containment", () => {
  let directory = "";

  beforeEach(() => {
    directory = mkdtempSync(path.join(tmpdir(), "rnl-snapshot-"));
  });

  afterEach(() => {
    rmSync(directory, { force: true, recursive: true });
  });

  const compare = (snapshot: string): readonly string[] =>
    compareSnapshot(directory, { artifactName: "observed.json", directory, observed: [], snapshot });

  it("refuses a name that resolves outside the goldens directory", () => {
    expect(compare("nested/../../escaped.json")[FIRST_FAILURE]).toContain("resolves outside");
  });

  it("reads a name that stays inside it", () => {
    writeFileSync(path.join(directory, "tree.json"), "[]");

    expect(compare("tree.json")).toEqual([]);
  });
});
