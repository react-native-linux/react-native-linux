# Platform identity: `Platform.OS === 'linux'`

Tracks issue #29 and ADR-0001 decision 9. Read `docs/research/codegen-and-oot-platform-tooling.md` §2–4 first; this
document only records what actually landed and why, not the survey.

## What exists today

- `packages/core/index.ts` — the entry point Metro resolves `react-native` to for platform `linux` (see
  "Injection mechanism" below). It currently exports only the Linux-specific surface this package owns:
  `Platform` and the `PlatformOSType`/`PlatformSelectSpec` types. It is **not** a complete `react-native`
  re-implementation yet — see "What this is not" below.
- `packages/core/src-linux/Libraries/Utilities/PlatformTypes.ts` — a derived override of upstream's
  `Libraries/Utilities/PlatformTypes.js`. Adds `'linux'` to `PlatformOSType` (upstream's union is
  `'ios' | 'android' | 'macos' | 'windows' | 'web' | 'native'`, verified against `v0.87.1`; there is no `'linux'`
  member, so this patch is permanent, not a temporary shim to drop on the next upgrade). Ported from Flow to
  TypeScript; only the `LinuxPlatform` variant is kept (the iOS/Android/Windows/macOS/Web variant shapes are not
  reproduced, since nothing in this repository consumes them and an unused export would fail `pnpm deadcode`).
- `packages/core/src-linux/Libraries/Utilities/NativePlatformConstantsLinux.ts` — the TurboModule-shaped spec
  (`PlatformConstantsLinux`, `NativePlatformConstantsLinuxSpec`) that a future native `PlatformConstants` module
  (issue #23) will satisfy. No `TurboModuleRegistry` lookup is wired yet — see "Deferred: native constants" below.
- `packages/core/src-linux/Libraries/Utilities/Platform.linux.ts` — a derived override of upstream's
  `Platform.android.js`, adapted to the spec above. `Platform.OS` is the literal `'linux'`; `isTV` and `isVision`
  are always `false`; `constants`/`Version`/`isTesting`/`isDisableAnimations` read from
  `NativePlatformConstantsLinuxSpec.getConstants()` when a native module is present, and fall back to a static
  constants object otherwise (`resolveLinuxPlatformConstants`, unit-tested directly).
- `packages/core/overrides.json` — the **react-native-platform-override** manifest (see "Override tooling"
  below) tracking the three files above plus `index.ts`, against `baseVersion: "0.87.1"`.
- `packages/cli/src/metro-config.ts` — `resolveLinuxOverlay(moduleName, platform, overlayIndex)`, a pure function
  used to prefer the `src-linux` overlay over the plain package-name redirect for the handful of `react-native`
  deep imports (`react-native/Libraries/Utilities/Platform`, `.../PlatformTypes`) this package currently overrides.
  100%-covered in `packages/cli/src/metro-config.spec.ts`.

## Injection mechanism, and why this one

Two real mechanisms exist upstream; we use both, layered:

1. **Package-name redirect** — already built into React Native core
   (`@react-native/community-cli-plugin`'s `reactNativePlatformResolver`, wired in automatically once
   `react-native.config.js` registers `platforms.linux.npmPackageName` — already true today in
   `packages/cli/src/platform-config.ts`). When Metro bundles for `platform: 'linux'`, any `moduleName === 'react-native'`
   or starting with `'react-native/'` gets rewritten to `@react-native-linux/core` (or
   `@react-native-linux/core/<subpath>`). This is "free": it comes from RN core, not from us, and requires no patch
   step against `node_modules/react-native`.
2. **`src-linux` overlay** — our addition, `resolveLinuxOverlay`. The package-name redirect alone would send
   `react-native/Libraries/Utilities/Platform` to `@react-native-linux/core/Libraries/Utilities/Platform`, a path
   that does not exist in our package (we do not vendor a full copy of `Libraries/**`, see below). The overlay
   index maps the small number of subpaths we actually override to their real file under
   `src-linux/Libraries/...`, and the composed resolver (documented, not yet wired into an actual `metro.config.js`
   — there is no app package yet, see "Deferred" below) tries the overlay first and falls through to the plain
   redirect otherwise.

This mirrors `react-native-windows`'s two-layer structure (`vnext/index.windows.js` + `vnext/src-win/**` +
`packages/react-native/react-native.config.js`'s `platforms` key), not `react-native-harmony`'s hand-rolled
`resolveRequest` that reimplements the package redirect from scratch, and not `react-native-macos`'s full
monorepo fork. RNW is the closer model because it keeps the override surface auditable (`overrides.json`) instead
of an unaudited full copy, which is what ADR-0001 decision 9 explicitly commits to.

A "patch `node_modules/react-native` in place" alternative was rejected: it would mutate a dependency outside
version control, is invisible to `overrides.json`-style tracking, and every other maintained out-of-tree platform
researched (§2 of the research doc) resolves through Metro instead.

## What this is not (and the real reason why)

`react-native-windows`'s published package is a **complete** `react-native` replacement: its `index.windows.js`
`require()`s well over 100 component/API files, and the ones it does not list in `overrides.json` (only ~123 of
several hundred files in `src-win/Libraries/**`) are copied in verbatim from upstream `react-native` by RNW's own
build tooling (`generate.js`) before publish. That copy step is real, load-bearing, and not something this change
builds — it is a "JS fork obligation" cost the ADR names explicitly and defers.

Two structural reasons this slice does not attempt it:

1. **Scope.** Issue #29's acceptance criteria are `Platform.OS === 'linux'`, `.linux` suffix resolution, and the
   override manifest for the files that exist today — not full API parity. The remaining ~11 tracked overrides
   (`BaseViewConfig` and the rest of the ~13-file set referenced in the issue) land with the components that
   consume them, per their own issues, the same incremental way RNW itself took years to reach 123 tracked files.
2. **A hard self-reference constraint.** `reactNativePlatformResolver` rewrites `moduleName === 'react-native'`
   (and any `'react-native/...'` deep import) unconditionally whenever the active Metro platform is `'linux'` —
   it does not look at which module is doing the requiring. If `@react-native-linux/core`'s own source imported
   `'react-native'` or `'react-native/Libraries/...'` internally (the natural way to write `export * from
   'react-native'`), that import would be rewritten right back to `@react-native-linux/core`, at best resolving to
   itself and at worst throwing "module not found" once a real `metro.config.js` exists. `react-native-windows`
   never hits this because it never imports bare `react-native` from inside `vnext/`; every internal reference is
   relative, into its own (fully vendored) tree. Until we either build the same vendoring step or add an
   origin-aware guard to our own composed resolver (skip the redirect when the requesting module is already inside
   `@react-native-linux/core`), `index.ts` can only safely export the files it genuinely owns. This is why
   `Platform.linux.ts` and `NativePlatformConstantsLinux.ts` also do not import anything from `react-native` at
   runtime — only `import type` from local sibling files.

## Override tooling: react-native-platform-override

**Version pinned:** `0.84.0` (published 2026-06-18, current `latest` — verified via `npm view
react-native-platform-override version`). Added to the `catalog:` in `pnpm-workspace.yaml` and as a
`devDependency` of `packages/core`.

**Hash algorithm, verified from source** (`packages/react-native-platform-override/src/Hash.ts` on
`microsoft/react-native-windows@main`): SHA-1 over the file content, after normalizing bare `\n` to `\r\n`
("line-ending insensitivity", the tool's default). This was cross-checked, not assumed: hashing
`Libraries/Utilities/Platform.android.js` at `v0.87.1` with this exact algorithm reproduces
`react-native-windows`'s own recorded `baseHash` for that same file
(`6497ec623691b34885e226e4d05bc55ba582a135`) byte-for-byte — the file has not changed between their pinned
nightly and `v0.87.1`. All three `baseHash` values in `packages/core/overrides.json` were computed this way
against `v0.87.1`, not left as placeholders.

**Manifest** (`packages/core/overrides.json`): `baseVersion: "0.87.1"`, `includePatterns: ["src-linux/**",
"index.ts"]`. Four entries: `index.ts` as `platform` (no upstream counterpart, given the scoping above),
`Platform.linux.ts` and `PlatformTypes.ts` as `derived` from their Android/shared upstream counterparts, and
`NativePlatformConstantsLinux.ts` as `derived` from
`src/private/specs_DEPRECATED/modules/NativePlatformConstantsAndroid.js` (matching the exact base file RNW itself
derives `NativePlatformConstantsWindows.js` from).

**Commands the coordinator runs**, in `packages/core`, after `pnpm install` at the repo root (the tool itself is
not installed until then):

```bash
pnpm install
pnpm --filter @react-native-linux/core override:validate   # react-native-platform-override validate
```

`validate` checks that every file matching `includePatterns` is listed, and that `derived`/`patch` entries'
`baseHash` still matches what `add`/`upgrade` would compute. It needs network access to GitHub to fetch base
files at the pinned version (`--githubToken` / `PLATFORM_OVERRIDE_GITHUB_TOKEN` if rate-limited). The tool
clones the react-native repository into `os.tmpdir()`, so on machines where `/tmp` is a small tmpfs point
`TMPDIR` at a directory with room for the clone — and never at a path inside a git repository, because the
tool runs git commands in that directory before it is initialised and they would resolve to the enclosing
repository instead:

```bash
mkdir -p ~/.cache/react-native-linux-tmp
TMPDIR=~/.cache/react-native-linux-tmp pnpm --filter @react-native-linux/core override:validate
``` When React
Native is upgraded past `0.87.1`, run `react-native-platform-override upgrade` from `packages/core` to
merge upstream changes into the three `derived` files and bump `baseVersion`.

## Deferred

- **Native `PlatformConstants` module (issue #23).** `nativePlatformConstantsLinux` in `Platform.linux.ts` is a
  hardcoded `null`; `resolveLinuxPlatformConstants` always takes the static-fallback branch today. Wiring the
  real lookup requires both the native module landing and a decision on the self-reference guard above (since a
  real lookup needs `TurboModuleRegistry`, which lives under `react-native/Libraries/TurboModule/...`).
- **Metro registration end-to-end (issue #22).** There is no `metro.config.js` anywhere in this repository yet —
  no app package exists to hold one. `resolveLinuxOverlay` and `linuxOverlayIndex` are written and 100%-tested as
  pure functions; composing them with `reactNativePlatformResolver` into an actual `resolver.resolveRequest`
  (including the origin-aware guard described above, and adapting to Metro's real `CustomResolver`/`Resolution`
  types, which are not a dependency of this repo yet) is that issue's job, not this one's.
- **The remaining ~11 core JS overrides.** Land with the component/API issue that needs them (e.g. `BaseViewConfig`
  with the first Fabric component that needs prop parity), each adding one more `overrides.json` entry and one
  more `linuxOverlayIndex` key — not speculatively ahead of a real consumer.
- **The full-package vendoring step.** RNW's `generate.js`-style copy of the rest of upstream `Libraries/**` into
  `src-linux` before publish is not built. `@react-native-linux/core` is not yet a complete drop-in `react-native`
  replacement, and is not claimed to be one; see "What this is not" above.

