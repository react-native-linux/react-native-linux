import { describe, expect, it } from "vitest";
import { resolveInjectionFailure, resolveWindowFlags } from "./grade.ts";

const waitForExpectedClose =
  (closed: boolean): (() => Promise<boolean>) =>
  () =>
    Promise.resolve(closed);

describe("resolveWindowFlags", () => {
  it("defaults an omitted field to --no-decorations", () => expect(resolveWindowFlags()).toEqual(["--no-decorations"]));

  it("keeps an explicit empty array as the compositor default", () => expect(resolveWindowFlags([])).toEqual([]));

  it("uses an explicit list exactly as written", () =>
    expect(resolveWindowFlags(["--force-client-decorations"])).toEqual(["--force-client-decorations"]));
});

describe("resolveInjectionFailure", () => {
  it("passes a null failure straight through", async () => {
    expect(await resolveInjectionFailure(null, true, waitForExpectedClose(true))).toBeNull();
  });

  it("keeps a failure the scenario never opted into forgiving", async () => {
    expect(await resolveInjectionFailure("boom", false, waitForExpectedClose(true))).toBe("boom");
  });

  it("forgives the failure once the expected close is observed", async () => {
    expect(await resolveInjectionFailure("boom", true, waitForExpectedClose(true))).toBeNull();
  });

  it("keeps the failure when the expected close never shows up", async () => {
    expect(await resolveInjectionFailure("boom", true, waitForExpectedClose(false))).toBe("boom");
  });
});
