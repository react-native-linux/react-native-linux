import { deriveFormatArtifactNames, derivePackageName, formatNameRules } from "./linux-package-format-names.ts";
import { describe, expect, it } from "vitest";

import type { LinuxBundleManifest } from "./linux-bundle-manifest.ts";

const baseManifest: LinuxBundleManifest = {
  applicationIdentifier: "org.example.LinuxApp",
  categories: ["Utility"],
  displayName: "Linux App",
  homepage: "https://example.org",
  iconSourcePaths: ["assets/icon.png"],
  licence: "MIT",
  longDescription: "A longer description of what the application does.",
  shortDescription: "A short description.",
  version: "1.0.0",
};

describe("derivePackageName", () => {
  it("kebab-cases the last reverse-DNS segment of the identifier", () => {
    expect(derivePackageName(baseManifest)).toBe("linux-app");
  });

  it("is unaffected by the display name", () => {
    const renamed: LinuxBundleManifest = { ...baseManifest, displayName: "Something Else Entirely" };

    expect(derivePackageName(renamed)).toBe(derivePackageName(baseManifest));
  });
});

describe("deriveFormatArtifactNames", () => {
  it("produces a lowercase .deb package name and the Debian architecture tokens", () => {
    const amd64 = deriveFormatArtifactNames(baseManifest, "deb", "x86_64");
    const arm64 = deriveFormatArtifactNames(baseManifest, "deb", "aarch64");

    expect(amd64.packageName).toBe("linux-app");
    expect(amd64.packageName).toBe(amd64.packageName.toLowerCase());
    expect(amd64.architectureToken).toBe("amd64");
    expect(amd64.artifactFilename).toBe("linux-app_1.0.0_amd64.deb");
    expect(arm64.architectureToken).toBe("arm64");
    expect(arm64.artifactFilename).toBe("linux-app_1.0.0_arm64.deb");
  });

  it("produces the pacman architecture tokens", () => {
    const x86 = deriveFormatArtifactNames(baseManifest, "pacman", "x86_64");
    const arm = deriveFormatArtifactNames(baseManifest, "pacman", "aarch64");

    expect(x86.architectureToken).toBe("x86_64");
    expect(x86.artifactFilename).toBe("linux-app-1.0.0-1-x86_64.pkg.tar.zst");
    expect(arm.architectureToken).toBe("aarch64");
    expect(arm.artifactFilename).toBe("linux-app-1.0.0-1-aarch64.pkg.tar.zst");
  });

  it("leaves every identity-derived name untouched by a display-name change", () => {
    const renamed: LinuxBundleManifest = { ...baseManifest, displayName: "Something Else Entirely" };

    expect(deriveFormatArtifactNames(renamed, "deb", "x86_64")).toStrictEqual(
      deriveFormatArtifactNames(baseManifest, "deb", "x86_64"),
    );
    expect(deriveFormatArtifactNames(renamed, "pacman", "aarch64")).toStrictEqual(
      deriveFormatArtifactNames(baseManifest, "pacman", "aarch64"),
    );
  });

  it("has a rule table entry for every declared package format", () => {
    expect(Object.keys(formatNameRules)).toStrictEqual(["deb", "pacman"]);
  });
});
