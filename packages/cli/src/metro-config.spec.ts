import { describe, expect, it } from "vitest";

import { linuxOverlayIndex, resolveLinuxOverlay } from "./metro-config.ts";

const overlayIndex = {
  "Libraries/Utilities/Platform": "/repo/packages/core/src-linux/Libraries/Utilities/Platform.linux.ts",
};

describe("resolveLinuxOverlay", () => {
  it("resolves a react-native deep import that has a linux overlay", () => {
    expect(resolveLinuxOverlay("react-native/Libraries/Utilities/Platform", "linux", overlayIndex)).toBe(
      overlayIndex["Libraries/Utilities/Platform"],
    );
  });

  it("returns null when the platform is not linux", () => {
    expect(resolveLinuxOverlay("react-native/Libraries/Utilities/Platform", "android", overlayIndex)).toBeNull();
  });

  it("returns null when the platform is not set", () => {
    expect(resolveLinuxOverlay("react-native/Libraries/Utilities/Platform", null, overlayIndex)).toBeNull();
  });

  it("returns null for module names outside the react-native package", () => {
    expect(resolveLinuxOverlay("react-native-gesture-handler", "linux", overlayIndex)).toBeNull();
  });

  it("returns null when the requested react-native subpath has no overlay entry", () => {
    expect(resolveLinuxOverlay("react-native/Libraries/Components/View/View", "linux", overlayIndex)).toBeNull();
  });
});

describe("linuxOverlayIndex", () => {
  it("maps the tracked Platform override to its src-linux file", () => {
    expect(linuxOverlayIndex["Libraries/Utilities/Platform"]).toMatch(
      /src-linux[/\\]Libraries[/\\]Utilities[/\\]Platform\.linux\.ts$/u,
    );
  });

  it("maps the tracked PlatformTypes override to its src-linux file", () => {
    expect(linuxOverlayIndex["Libraries/Utilities/PlatformTypes"]).toMatch(
      /src-linux[/\\]Libraries[/\\]Utilities[/\\]PlatformTypes\.ts$/u,
    );
  });
});
