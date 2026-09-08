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
  at `usr/share/icons/hicolor/<w>x<h>[@2]/apps/<applicationIdentifier>.png`, and picks the largest square icon as
  the directory icon (the AppImage `.DirIcon` selection). A source set with no square icon throws
  `NoSquareIconError` by name — the AppImage bundler path this mirrors panics instead. `readPngDimensions` decodes
  a PNG buffer with `pngjs`; `loadIconSource` is the thin disk-reading wrapper around it.

Both generators are pure functions over their inputs: no filesystem or process side effects beyond
`loadIconSource`'s read. Validate a generated entry against the specification with `desktop-file-validate` (part
of `desktop-file-utils`, present in CI):

```bash
desktop-file-validate ./org.example.App.desktop
```

**Not yet built** (left for follow-up on #356 — see the issue's acceptance criteria for the full list): the
manifest schema these generators' inputs are drawn from at package time (#362 owns the reverse-DNS identifier
field, display name, categories and icon source list as one validated schema); the package-time writer that lays
the generated entry and icon tree onto disk; post-install/post-remove hooks that refresh the icon cache and the
desktop database; and the end-to-end assertion that an installed package's entry, launched under the headless
compositor, reports back an `app_id` equal to the entry's basename.
