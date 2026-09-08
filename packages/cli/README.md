# @react-native-linux/cli

Metro platform registration (`platform-config.ts`, `metro-config.ts`) and the packaging generators that turn a
Linux bundle manifest into installable artefacts. Tracks issue #356 (and its merged duplicate #366) under #176.

## Desktop entry and hicolor icon tree (#356)

Wayland gives an application no way to set its own name or icon; both come from an installed `.desktop` entry
that the compositor matches to the running surface by `xdg_toplevel.set_app_id`
(`packages/core/src/WaylandWindow.{h,cpp}`, `--app-id`, tracked in `docs/cpp-toolchain.md` under *Decorations and
app_id (#329)*). **The identity invariant is one string:** the desktop entry's basename, without the `.desktop`
suffix, equals the `--app-id` value passed to the binary. Per the acceptance amendment from PR #387's review,
this is the *only* general Wayland rule — `StartupWMClass` is a separate, desktop-environment-specific
compatibility hint (still emitted, equal to the identifier, but not the thing the compositor actually matches
on), and `Icon` is a separately resolved resource key per the Desktop Entry Specification: either an absolute
path or a themed icon name, neither required to equal the identifier or the basename.

- `src/desktop-entry.ts` — `generateDesktopEntry(manifest)` renders `[Desktop Entry]` (`Type`, `Name`, `Comment`,
  `Exec`, `Icon`, `Categories`, `Terminal=false`, `StartupWMClass`) from a `DesktopEntryManifest`. `MimeType` is
  emitted, and `Exec` gains the `%u` or `%f` field code, only when `urlSchemes` or `fileAssociationMimeTypes` are
  declared — `%u` wins when both are present, since it already handles local paths as well as URIs. This is the
  direct fix for tauri#15928, where the upstream template declares `MimeType=x-scheme-handler/…` without ever
  adding a field code to `Exec`, so the desktop environment silently drops the activation URL. `Icon` is
  validated against the Desktop Entry Specification's two legal shapes (absolute path, or a themed name with no
  path separator) and throws `InvalidIconValueError` by name rather than emitting a spec-violating file. List
  values (`Categories`, `MimeType`) and free-text values (`Name`, `Comment`) follow the specification's escaping
  rules for backslash, semicolon and control characters.
- `src/hicolor-icon-tree.ts` — `layoutHicolorIconTree(icons, applicationIdentifier)` reads each source icon's
  *real* dimensions rather than trusting a filename (Tauri's `list_icon_files` is the reference), lays each out
  at `usr/share/icons/hicolor/<nominal-w>x<nominal-h>[@2]/apps/<applicationIdentifier>.png` — the nominal size is
  the source's pixel size divided by its scale, so a 256px source at scale 2 lands in the `128x128@2` directory,
  not `256x256@2` — and picks the largest square icon as the directory icon (the AppImage `.DirIcon` selection).
  Every source must be square: a non-square source fails the whole call by name (`NoSquareIconError`, naming the
  offending source and its dimensions) rather than being silently skipped, and an empty source list fails the
  same way — the AppImage bundler path this mirrors panics instead. `readPngDimensions` decodes a PNG buffer with
  `pngjs`; `loadIconSource` is the thin disk-reading wrapper around it.

Both generators are pure functions over their inputs: no filesystem or process side effects beyond
`loadIconSource`'s read. Validate a generated entry against the specification with `desktop-file-validate` (part
of `desktop-file-utils`, present in CI):

```bash
desktop-file-validate ./org.example.App.desktop
```

**Not yet built** (left for follow-up on #356 — see the issue's acceptance criteria for the full list): the
package-time writer that lays the generated entry and icon tree onto disk; post-install/post-remove hooks that
refresh the icon cache and the desktop database; and the end-to-end assertion that an installed package's entry,
launched under the headless compositor, reports back an `app_id` equal to the entry's basename.

## Linux bundle manifest (#362)

Six things have to agree on what the application is called — the executable, the `.desktop` entry, `--app-id`,
the D-Bus well-known name, the package name, and the artefact filenames — and each package format has its own
naming rules on top (case, allowed characters, architecture spelling). `src/linux-bundle-manifest.ts` is the one
schema this is read from, so identity is invented once rather than at every point of use.

- `LinuxBundleManifest` (`src/linux-bundle-manifest.ts`) holds `applicationIdentifier` (a reverse-DNS identifier
  — the same field `DesktopEntryManifest.applicationIdentifier` (#356) is fed from), `displayName` (free text,
  used only for the `Name` key — renaming it never changes anything identity-derived), `version` (semantic),
  `categories`, `shortDescription`, `longDescription`, `homepage`, `licence`, `iconSourcePaths` (fed to
  `layoutHicolorIconTree`), and the optional `urlSchemes`, `fileAssociationMimeTypes` and
  `extraRuntimeDependencies`. `validateLinuxBundleManifest(candidate)` checks every field, rejects an unrecognized
  key instead of ignoring it, and throws `LinuxBundleManifestValidationError` naming every failing field at once
  (`fieldErrors`) rather than stopping at the first one — the tauri#13999 lesson is that identity and display
  name being the same field is itself the bug, so the two are validated, and used, separately.
- `linuxBundleManifestJsonSchema` is the canonical JSON Schema, generated from the same field-descriptor table the
  validator runs against (so the two cannot drift from each other) and checked in at
  `schemas/linux-bundle-manifest.schema.json`. `pnpm manifest-schema:check` (part of `pnpm validate`) fails the
  build when the checked-in file is stale; `pnpm manifest-schema:generate` regenerates it — the
  `check-generated-files` shape tauri-utils uses for its own config struct.
- `src/linux-package-format-names.ts` derives per-format names from the manifest rather than taking them as
  input: `derivePackageName(manifest)` kebab-cases the *last* reverse-DNS segment of `applicationIdentifier` —
  never `displayName` — so a display-name change leaves the package name, and every artefact filename built from
  it, untouched. `deriveFormatArtifactNames(manifest, format, hostArchitecture)` looks up one rule table entry per
  format (`formatNameRules`, a `Record` covering every declared `LinuxPackageFormat`, so `pnpm ts` fails a build
  that adds a format without adding its rules) and returns the package name, the architecture token, and the
  artefact filename:
  - `deb` — `amd64` / `arm64`, `<name>_<version>_<arch>.deb` (Debian convention).
  - `pacman` — `x86_64` / `aarch64`, `<name>-<version>-1-<arch>.pkg.tar.zst` (Arch convention, the AUR channel
    #25 packages against). This is also the direct fix for tauri#10031 (an AppImage bundler spelling `aarch64`
    where the convention is `arm64`) and tauri#12073 (a `.deb` name that is allowed to contain uppercase letters):
    each format's case and architecture spelling live in exactly one place.
