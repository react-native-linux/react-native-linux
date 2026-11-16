import { ExpoKeepAwakeTag, activateKeepAwakeAsync, deactivateKeepAwake, useKeepAwake } from "./expo-keep-awake.ts";
import { describe, expect, it } from "vitest";

const VOICE_TAG = "voice-input";

describe("the expo-keep-awake stand-in", () => {
  it("resolves VoiceInputProvider's activate and deactivate for its tag", async () => {
    await expect(activateKeepAwakeAsync(VOICE_TAG)).resolves.toBeUndefined();
    await expect(deactivateKeepAwake(VOICE_TAG)).resolves.toBeUndefined();
  });

  it("answers withDevTools' hook with Expo's default tag and keeps nothing awake", () => {
    expect([ExpoKeepAwakeTag, useKeepAwake()]).toStrictEqual(["ExpoKeepAwakeDefaultTag", undefined]);
  });
});
