import { describe, expect, it, vi } from "vitest";
import { Accelerometer } from "./expo-sensors.ts";

const SHAKE_INTERVAL_MS = 50;

describe("the expo-sensors stand-in", () => {
  it("reads the accelerometer unavailable", async () => {
    await expect(Accelerometer.isAvailableAsync()).resolves.toBe(false);
  });

  it("takes the device preview's interval and subscription and never delivers a sample", () => {
    const listener = vi.fn<() => void>();

    Accelerometer.setUpdateInterval(SHAKE_INTERVAL_MS);
    Accelerometer.addListener(listener).remove();

    expect(listener).not.toHaveBeenCalled();
  });
});
