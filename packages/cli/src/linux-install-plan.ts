import type { HicolorIconTree } from "./hicolor-icon-tree.ts";
import type { LinuxBundleManifest } from "./linux-bundle-manifest.ts";
import { generateDesktopEntry } from "./desktop-entry.ts";

/**
 * One staged file of the installed tree: either generated text (`contents`) or a file copied verbatim
 * (`sourcePath`), never both — a writer that could do both would have two answers for what one destination holds.
 */
interface LinuxInstallFile {
  readonly contents: string | null;
  readonly destinationPath: string;
  readonly sourcePath: string | null;
}

interface LinuxInstallPlan {
  /**
   * The one value the identity invariant is about: the desktop entry's basename, the entry's `Icon` key, its
   * `StartupWMClass` and the `xdg_toplevel.set_app_id` the running window sends are all this string, read from
   * the manifest's single `applicationIdentifier` field. `assertLinuxInstallIdentity` is what stops a writer or
   * an e2e from shipping a plan whose parts were allowed to diverge.
   */
  readonly applicationIdentifier: string;
  readonly files: readonly LinuxInstallFile[];
}

class IdentityMismatchError extends Error {
  public constructor(message: string) {
    super(message);
    this.name = "IdentityMismatchError";
  }
}

const applicationsDirectory = "usr/share/applications";
const keySeparatorLength = 1;

const identityLines = (contents: string, key: string): string | null => {
  for (const candidate of contents.split("\n")) {
    if (candidate.startsWith(`${key}=`)) {
      return candidate.slice(key.length + keySeparatorLength);
    }
  }

  return null;
};

/**
 * The identity invariant as a pure assertion over an entry's text: the basename the entry installs under, the
 * `Icon` key and the `StartupWMClass` hint must all be the application identifier — the same value
 * `xdg_toplevel.set_app_id` carries, which is what lets the compositor match the running surface to the
 * installed entry (the Wayland invariant #387 amended the acceptance to; `StartupWMClass` is the
 * desktop-environment fallback for matchers that never read Wayland).
 */
const assertLinuxInstallIdentity = (contents: string, applicationIdentifier: string): void => {
  const icon = identityLines(contents, "Icon");
  const startupWmClass = identityLines(contents, "StartupWMClass");

  if (icon !== applicationIdentifier) {
    throw new IdentityMismatchError(
      `the desktop entry's Icon key is ${icon === null ? "absent" : `"${icon}"`}, not the application identifier "${applicationIdentifier}"`,
    );
  }

  if (startupWmClass !== applicationIdentifier) {
    throw new IdentityMismatchError(
      `the desktop entry's StartupWMClass is ${startupWmClass === null ? "absent" : `"${startupWmClass}"`}, not the application identifier "${applicationIdentifier}"`,
    );
  }
};

/**
 * The package-time half of #356: one plan of staged files from the manifest and the laid-out icon tree. The
 * desktop entry is generated with the identifier as its `Icon` — the themed name the tree's paths install the
 * icons under — so the compositor asking for any standard size resolves a file whose themed name is the running
 * application. The executable path is the caller's: the manifest says nothing about where the package puts the
 * binary, and inventing a convention here would be one more place to disagree with the package builder about.
 */
const planLinuxInstall = (
  manifest: LinuxBundleManifest,
  iconTree: HicolorIconTree,
  executablePath: string,
): LinuxInstallPlan => {
  const entry = generateDesktopEntry({
    applicationIdentifier: manifest.applicationIdentifier,
    categories: manifest.categories,
    comment: manifest.shortDescription,
    displayName: manifest.displayName,
    exec: executablePath,
    icon: manifest.applicationIdentifier,
    ...("fileAssociationMimeTypes" in manifest ? { fileAssociationMimeTypes: manifest.fileAssociationMimeTypes } : {}),
    ...("urlSchemes" in manifest ? { urlSchemes: manifest.urlSchemes } : {}),
  });

  assertLinuxInstallIdentity(entry.contents, manifest.applicationIdentifier);

  // The tree can belong to another application: an identifier whose themed name nothing staged carries points the entry at an icon never installed.
  for (const icon of iconTree.entries) {
    if (!icon.installPath.endsWith(`/apps/${manifest.applicationIdentifier}.png`)) {
      throw new IdentityMismatchError(
        `the icon tree installs "${icon.installPath}", not the application identifier's themed name "${manifest.applicationIdentifier}.png"`,
      );
    }
  }

  return {
    applicationIdentifier: manifest.applicationIdentifier,
    files: [
      {
        contents: entry.contents,
        destinationPath: `${applicationsDirectory}/${entry.basename}`,
        sourcePath: null,
      },
      ...iconTree.entries.map((icon) => ({
        contents: null,
        destinationPath: icon.installPath,
        sourcePath: icon.sourcePath,
      })),
    ],
  };
};

export { assertLinuxInstallIdentity, IdentityMismatchError, planLinuxInstall };
