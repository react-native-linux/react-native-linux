import { describe, expect, it } from "vitest";
import { findAccessibilityChanges, gradeAccessibilityChanges } from "./accessibility-changes.ts";

const SOCKET_PATH = "/run/user/1000/rnl-automation-9.sock";
const FIRST_FAILURE = 0;
const EXPECTATION = [{ state: true, testID: "toggle", value: false }];

const fakeRequest =
  (result: Record<string, unknown> | null, failure: string | null = null) =>
  (): Promise<{ readonly failure: string | null; readonly result: Record<string, unknown> | null }> =>
    Promise.resolve({ failure, result });

describe("findAccessibilityChanges", () => {
  it("takes the recorded changes", () => {
    expect(findAccessibilityChanges({ changes: [{ tag: 2 }] })).toEqual([{ tag: 2 }]);
  });

  it("takes nothing from an answer whose changes are not a list", () => {
    expect(findAccessibilityChanges({})).toBeNull();
  });
});

describe("gradeAccessibilityChanges, the request side", () => {
  it("asks nothing when the scenario names no expected change", async () => {
    expect(await gradeAccessibilityChanges(fakeRequest(null, "unreachable"), SOCKET_PATH, null)).toEqual([]);
  });

  it("passes when the channel recorded exactly the expected change", async () => {
    const request = fakeRequest({ changes: [{ state: true, testID: "toggle" }] });

    expect(await gradeAccessibilityChanges(request, SOCKET_PATH, EXPECTATION)).toEqual([]);
  });

  it("fails when the window refuses the request", async () => {
    const failures = await gradeAccessibilityChanges(
      fakeRequest(null, "no bundle is running"),
      SOCKET_PATH,
      EXPECTATION,
    );

    expect(failures).toEqual(["no bundle is running"]);
  });

  it("fails when the window answers without a changes list", async () => {
    const failures = await gradeAccessibilityChanges(fakeRequest({}), SOCKET_PATH, EXPECTATION);

    expect(failures[FIRST_FAILURE]).toContain('without a "changes" list');
  });
});

describe("gradeAccessibilityChanges, comparing what arrived", () => {
  it("fails when the expected change never arrived", async () => {
    const failures = await gradeAccessibilityChanges(fakeRequest({ changes: [] }), SOCKET_PATH, EXPECTATION);

    expect(failures[FIRST_FAILURE]).toContain("accessibilityChanges does not match");
  });

  it("fails when the recorded change carries the wrong half", async () => {
    const request = fakeRequest({ changes: [{ testID: "toggle", value: true }] });
    const failures = await gradeAccessibilityChanges(request, SOCKET_PATH, EXPECTATION);

    expect(failures[FIRST_FAILURE]).toContain("accessibilityChanges does not match");
  });

  it("treats a change that is not an object as carrying nothing", async () => {
    const request = fakeRequest({ changes: ["not an object"] });
    const failures = await gradeAccessibilityChanges(request, SOCKET_PATH, EXPECTATION);

    expect(failures[FIRST_FAILURE]).toContain("accessibilityChanges does not match");
  });

  it("treats a non-string testID as no testID at all", async () => {
    const request = fakeRequest({ changes: [{ state: true, testID: 7 }] });
    const failures = await gradeAccessibilityChanges(request, SOCKET_PATH, EXPECTATION);

    expect(failures[FIRST_FAILURE]).toContain("accessibilityChanges does not match");
  });
});
