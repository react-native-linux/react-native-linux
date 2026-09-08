import { describe, expect, it } from "vitest";
import { hooksForFormat } from "./linux-package-hooks.ts";

describe("hooksForFormat", () => {
  it("refreshes the icon cache and the desktop database after install and removal for deb", () => {
    const hooks = hooksForFormat("deb");

    expect(hooks.postInstall).toEqual([
      "if [ -x /usr/bin/gtk-update-icon-cache ]; then gtk-update-icon-cache -q /usr/share/icons/hicolor; fi",
      "if [ -x /usr/bin/update-desktop-database ]; then update-desktop-database -q; fi",
    ]);
    expect(hooks.postRemove).toEqual(hooks.postInstall);
  });

  it("runs the same refreshes for rpm", () => {
    expect(hooksForFormat("rpm").postInstall).toEqual(hooksForFormat("deb").postInstall);
  });

  it("gives pacman no hooks, because pacman ships both hooks itself", () => {
    const hooks = hooksForFormat("pacman");

    expect(hooks.postInstall).toEqual([]);
    expect(hooks.postRemove).toEqual([]);
  });
});
