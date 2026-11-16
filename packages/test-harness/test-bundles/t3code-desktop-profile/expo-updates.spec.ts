import { checkForUpdateAsync, fetchUpdateAsync, isEnabled, readLogEntriesAsync, reloadAsync } from "./expo-updates.ts";
import { describe, expect, it } from "vitest";

describe("the expo-updates stand-in", () => {
  it("reports updates disabled, which gates T3 Code's update checks and diagnostics log read", () => {
    expect(isEnabled).toBe(false);
  });

  it.each([
    ["checkForUpdateAsync", checkForUpdateAsync],
    ["fetchUpdateAsync", fetchUpdateAsync],
    ["readLogEntriesAsync", readLogEntriesAsync],
    ["reloadAsync", reloadAsync],
  ])("rejects %s with the code T3 Code reads as updates unavailable", async (_name, call) => {
    await expect(call()).rejects.toMatchObject({ code: "ERR_UPDATES_DISABLED" });
  });
});
