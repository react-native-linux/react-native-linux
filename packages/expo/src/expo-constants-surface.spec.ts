import { describe, expect, it } from "vitest";
import path from "node:path";
import { readFileSync } from "node:fs";
import { requiredConstantsFields } from "./expo-constants-surface.ts";

const scenario: unknown = JSON.parse(
  readFileSync(path.resolve(import.meta.dirname, "..", "e2e", "expo-constants.json"), "utf8"),
);

describe("the expo-constants surface on Linux", () => {
  it.each(requiredConstantsFields)("asserts a defined value in the e2e for the required field %s", (field) => {
    expect(scenario).toMatchObject({ expect: [expect.stringMatching(new RegExp(` ${field}=(?!undefined)`, "u"))] });
  });
});
