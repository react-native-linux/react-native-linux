import {
  Platform,
  resolveIsDisableAnimations,
  resolveLinuxPlatformConstants,
  selectLinuxPlatform,
} from "./Platform.linux.ts";

import { describe, expect, it } from "vitest";

describe("Platform.linux", () => {
  it("reports the linux platform identifier", () => {
    expect(Platform.OS).toBe("linux");
  });

  it("is never a TV platform", () => {
    expect(Platform.isTV).toBe(false);
  });

  it("is never a vision platform", () => {
    expect(Platform.isVision).toBe(false);
  });

  it("falls back to the static constants when isDisableAnimations is not reported", () => {
    expect(Platform.isDisableAnimations).toBe(Platform.isTesting);
  });

  it("reads the OS version from the constants fallback", () => {
    expect(Platform.Version).toBe(Platform.constants.osVersion);
  });

  it("reports the static fallback as not under test", () => {
    expect(Platform.isTesting).toBe(false);
  });
});

describe("resolveLinuxPlatformConstants", () => {
  it("returns the static fallback constants when the native module is absent", () => {
    expect(resolveLinuxPlatformConstants(null)).toEqual({
      isTesting: false,
      osVersion: "unknown",
      reactNativeVersion: { major: 0, minor: 0, patch: 0, prerelease: null },
    });
  });

  it("returns the native module constants when the native module is present", () => {
    const nativeConstants = {
      isDisableAnimations: true,
      isTesting: true,
      osVersion: "6.11.0",
      reactNativeVersion: { major: 0, minor: 87, patch: 1, prerelease: null },
    };

    expect(resolveLinuxPlatformConstants({ getConstants: () => nativeConstants })).toBe(nativeConstants);
  });
});

describe("resolveIsDisableAnimations", () => {
  it("reports the explicit value when the constants report one", () => {
    expect(
      resolveIsDisableAnimations({
        isDisableAnimations: true,
        isTesting: false,
        osVersion: "unknown",
        reactNativeVersion: { major: 0, minor: 0, patch: 0, prerelease: null },
      }),
    ).toBe(true);
  });

  it("falls back to isTesting when the constants do not report a value", () => {
    expect(
      resolveIsDisableAnimations({
        isTesting: true,
        osVersion: "unknown",
        reactNativeVersion: { major: 0, minor: 0, patch: 0, prerelease: null },
      }),
    ).toBe(true);
  });
});

describe("selectLinuxPlatform", () => {
  it("prefers the linux-specific value", () => {
    expect(selectLinuxPlatform({ default: "default", linux: "linux", native: "native" })).toBe("linux");
  });

  it("falls back to the native value when linux is not specified", () => {
    expect(selectLinuxPlatform({ default: "default", native: "native" })).toBe("native");
  });

  it("falls back to the default value when neither linux nor native is specified", () => {
    expect(selectLinuxPlatform({ default: "default" })).toBe("default");
  });
});
