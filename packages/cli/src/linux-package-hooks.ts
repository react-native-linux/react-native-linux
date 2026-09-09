/**
 * The post-install and post-remove scripts #356 asks for, per package format: the two caches a Wayland desktop
 * reads identity and imagery from go stale the moment files land in or leave the tree, and nothing inside the
 * process can refresh them.
 *
 * - deb and rpm run the two refreshes from their maintainer scripts and scriptlets, each guarded on the tool
 *   existing so a minimal container install does not fail the whole package for a helper it never shipped.
 * - pacman (the AUR channel #25 commits to) needs none: it ships `update-desktop-database.hook` and
 *   `gtk-update-icon-cache.hook` and runs both on every transaction that touches those directories — that is
 *   the documented reason, not an omission.
 */
interface LinuxPackageHooks {
  readonly postInstall: readonly string[];
  readonly postRemove: readonly string[];
}

type LinuxPackageFormat = "deb" | "pacman" | "rpm";

const hicolorIconDirectory = "/usr/share/icons/hicolor";
const applicationsDirectory = "/usr/share/applications";

const refreshCaches = (): readonly string[] => [
  `if [ -x /usr/bin/gtk-update-icon-cache ]; then gtk-update-icon-cache -q ${hicolorIconDirectory}; fi`,
  `if [ -x /usr/bin/update-desktop-database ]; then update-desktop-database -q ${applicationsDirectory}; fi`,
];

const hooksForFormat = (format: LinuxPackageFormat): LinuxPackageHooks => {
  if (format === "pacman") {
    return { postInstall: [], postRemove: [] };
  }

  return { postInstall: refreshCaches(), postRemove: refreshCaches() };
};

export { hooksForFormat };
