import { IdentityMismatchError, assertLinuxInstallIdentity, planLinuxInstall } from "./linux-install-plan.ts";
import { describe, expect, it } from "vitest";
import { doubleScale, layoutHicolorIconTree } from "./hicolor-icon-tree.ts";

const applicationIdentifier = "org.reactnativelinux.Flagship";
const executablePath = "/usr/bin/flagship";
const desktopEntryFileIndex = 0;
const firstIconFileIndex = 1;
const iconSize64 = 64;
const iconSize128 = 128;
const iconSize256 = 256;

interface SpecManifest {
  readonly applicationIdentifier: string;
  readonly categories: readonly string[];
  readonly displayName: string;
  readonly homepage: string;
  readonly iconSourcePaths: readonly string[];
  readonly licence: string;
  readonly longDescription: string;
  readonly shortDescription: string;
  readonly urlSchemes?: readonly string[];
  readonly fileAssociationMimeTypes?: readonly string[];
  readonly version: string;
}

const manifest: SpecManifest = {
  applicationIdentifier,
  categories: ["Game", "LogicGame"],
  displayName: "The Flagship",
  homepage: "https://example.com",
  iconSourcePaths: ["icon-64.png", "icon-256.png"],
  licence: "MIT",
  longDescription: "A long description",
  shortDescription: "A short description",
  version: "1.0.0",
};

interface PlannedFile {
  readonly contents: string | null;
  readonly destinationPath: string;
  readonly sourcePath: string | null;
}

const squareIcon = (
  size: number,
  scale?: typeof doubleScale,
): { height: number; sourcePath: string; width: number; scale?: typeof doubleScale } => ({
  height: size,
  sourcePath: `icon-${size}${scale === doubleScale ? "@2" : ""}.png`,
  width: size,
  ...(typeof scale === "number" ? { scale } : {}),
});

const iconTree = layoutHicolorIconTree(
  [squareIcon(iconSize64), squareIcon(iconSize128), squareIcon(iconSize256), squareIcon(iconSize256, doubleScale)],
  applicationIdentifier,
);

const plannedFiles = (overrides?: Partial<typeof manifest>): readonly PlannedFile[] =>
  planLinuxInstall({ ...manifest, ...overrides }, iconTree, executablePath).files;

describe("planLinuxInstall", () => {
  it("stages the desktop entry under the identifier's basename", () => {
    const files = plannedFiles();

    expect(files[desktopEntryFileIndex]?.destinationPath).toBe(
      "usr/share/applications/org.reactnativelinux.Flagship.desktop",
    );
    expect(files[desktopEntryFileIndex]?.contents).toContain("Exec=/usr/bin/flagship\n");
    expect(files[desktopEntryFileIndex]?.sourcePath).toBeNull();
  });

  it("derives the entry's Icon key and StartupWMClass from the application identifier alone", () => {
    const contents = plannedFiles()[desktopEntryFileIndex]?.contents ?? "";

    expect(contents).toContain(`Icon=${applicationIdentifier}\n`);
    expect(contents).toContain(`StartupWMClass=${applicationIdentifier}\n`);
  });

  it("carries the manifest's schemes and associations into the entry", () => {
    const contents =
      plannedFiles({ fileAssociationMimeTypes: ["application/x-flagship"], urlSchemes: ["flagship"] })[
        desktopEntryFileIndex
      ]?.contents ?? "";

    expect(contents).toContain("Exec=/usr/bin/flagship %u\n");
    expect(contents).toContain("MimeType=x-scheme-handler/flagship;application/x-flagship;\n");
  });

  it("copies every icon source into the hicolor tree at its claimed size and scale", () => {
    const files = plannedFiles();
    const destinations = files.slice(firstIconFileIndex).map((file) => file.destinationPath);

    expect(destinations).toEqual([
      "usr/share/icons/hicolor/64x64/apps/org.reactnativelinux.Flagship.png",
      "usr/share/icons/hicolor/128x128/apps/org.reactnativelinux.Flagship.png",
      "usr/share/icons/hicolor/256x256/apps/org.reactnativelinux.Flagship.png",
      "usr/share/icons/hicolor/128x128@2/apps/org.reactnativelinux.Flagship.png",
    ]);
    expect(files[firstIconFileIndex]?.sourcePath).toBe("icon-64.png");
    expect(files[firstIconFileIndex]?.contents).toBeNull();
    expect(files[desktopEntryFileIndex]?.sourcePath).toBeNull();
  });
});

describe("planLinuxInstall identity with the running application", () => {
  it("is the one value for set_app_id, the entry basename, Icon and StartupWMClass", () => {
    const plan = planLinuxInstall(manifest, iconTree, executablePath);

    expect(plan.applicationIdentifier).toBe(applicationIdentifier);
    expect(
      assertLinuxInstallIdentity(plan.files[desktopEntryFileIndex]?.contents ?? "", plan.applicationIdentifier),
    ).toBeUndefined();
  });

  it("rejects an icon tree that belongs to another application identifier", () => {
    const foreignTree = layoutHicolorIconTree([squareIcon(iconSize256)], "org.example.Other");

    expect(() => planLinuxInstall(manifest, foreignTree, executablePath)).toThrow(IdentityMismatchError);
    expect(() => planLinuxInstall(manifest, foreignTree, executablePath)).toThrow(
      /installs "usr\/share\/icons\/hicolor\/256x256\/apps\/org\.example\.Other\.png", not the application identifier's/u,
    );
  });
});

describe("assertLinuxInstallIdentity", () => {
  const contents = [
    "[Desktop Entry]",
    "Type=Application",
    "Name=The Flagship",
    `Exec=${executablePath}`,
    `Icon=${applicationIdentifier}`,
    "Terminal=false",
    `StartupWMClass=${applicationIdentifier}`,
    "",
  ].join("\n");

  it("passes when every identity key is the application identifier", () => {
    expect(() => assertLinuxInstallIdentity(contents, applicationIdentifier)).not.toThrow();
  });

  it("fails by name when the Icon key diverges", () => {
    const divergent = contents.replace(`Icon=${applicationIdentifier}`, "Icon=flagship-icon");

    expect(() => assertLinuxInstallIdentity(divergent, applicationIdentifier)).toThrow(IdentityMismatchError);
    expect(() => assertLinuxInstallIdentity(divergent, applicationIdentifier)).toThrow(
      /Icon key is "flagship-icon", not the application identifier/u,
    );
  });

  it("fails by name when StartupWMClass diverges", () => {
    const divergent = contents.replace(`StartupWMClass=${applicationIdentifier}`, "StartupWMClass=flagship");

    expect(() => assertLinuxInstallIdentity(divergent, applicationIdentifier)).toThrow(IdentityMismatchError);
    expect(() => assertLinuxInstallIdentity(divergent, applicationIdentifier)).toThrow(
      /StartupWMClass is "flagship", not the application identifier/u,
    );
  });

  it("fails by name when a key is absent", () => {
    expect(() => assertLinuxInstallIdentity("[Desktop Entry]\n", applicationIdentifier)).toThrow(/Icon key is absent/u);
    expect(() =>
      assertLinuxInstallIdentity(`[Desktop Entry]\nIcon=${applicationIdentifier}\n`, applicationIdentifier),
    ).toThrow(/StartupWMClass is absent/u);
  });
});
