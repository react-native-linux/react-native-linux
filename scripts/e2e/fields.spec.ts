import { describe, expect, it } from "vitest";
import { readOptionalStringArray } from "./fields.ts";

const NON_STRING_ENTRY = 1;

describe("readOptionalStringArray", () => {
  it("returns undefined, distinct from an explicit empty array, when the field is omitted", () => {
    expect(readOptionalStringArray({}, "windowFlags", "fixture.json")).toBeUndefined();
  });

  it("keeps an explicit empty array rather than treating it as omitted", () => {
    expect(readOptionalStringArray({ windowFlags: [] }, "windowFlags", "fixture.json")).toEqual([]);
  });

  it("reads an explicit non-empty array of strings", () => {
    expect(
      readOptionalStringArray({ windowFlags: ["--force-client-decorations"] }, "windowFlags", "fixture.json"),
    ).toEqual(["--force-client-decorations"]);
  });

  it("rejects a value that is not an array of strings", () => {
    expect(() => readOptionalStringArray({ windowFlags: "nope" }, "windowFlags", "fixture.json")).toThrow(
      'fixture.json: "windowFlags" must be an array of strings',
    );
  });

  it("rejects an array with a non-string entry", () => {
    expect(() => readOptionalStringArray({ windowFlags: [NON_STRING_ENTRY] }, "windowFlags", "fixture.json")).toThrow(
      'fixture.json: "windowFlags" must be an array of strings',
    );
  });
});
