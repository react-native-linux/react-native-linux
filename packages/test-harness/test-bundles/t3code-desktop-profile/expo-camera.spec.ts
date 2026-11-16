import { describe, expect, it } from "vitest";
import { CameraView, useCameraPermissions } from "./expo-camera.ts";

const denied = { canAskAgain: false, expires: "never", granted: false, status: "denied" };

describe("the expo-camera stand-in", () => {
  it("reads camera permission denied for good, so the pairing screen offers no second prompt", () => {
    const [permission] = useCameraPermissions();

    expect(permission).toStrictEqual(denied);
  });

  it("resolves a request and a read denied", async () => {
    const [, requestPermission, getPermission] = useCameraPermissions();

    await expect(requestPermission()).resolves.toStrictEqual(denied);
    await expect(getPermission()).resolves.toStrictEqual(denied);
  });

  it("draws no camera", () => {
    expect(CameraView()).toBeNull();
  });
});
