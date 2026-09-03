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
  The same file also has `resolvePlatformCandidates(moduleName, platform, sourceExts)` and
  `resolveAgainstFilesystem(candidates, exists)`, the pure functions for Metro's platform-extension fallback chain
  (issue #55) — see "Resolution order" below. Both are 100%-covered in `packages/cli/src/metro-config.spec.ts`,
  the latter against a committed fixture tree under `packages/cli/test-fixtures/resolution/`.
- `packages/cli/src/metro-config.ts` also carries the Reanimated bring-up rung's resolver shim —
  `shouldUseJavaScriptFallback(request)` and `resolveOriginAwareCandidates(request, sourceExts)` — see
  "Reanimated bring-up rung" below.
- `packages/cli/src/animation-rung.ts` — `describeAnimationRung()`, the single startup warning string the app
  template prints once. A pure export with no process-level side effects; nothing in this repository writes it to a
  stream yet, because nothing boots a JavaScript app yet.

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

## Resolution order: `.linux` → `.native` → default

Tracks issue #55, filed after `microsoft/react-native-macos#2778`: for nine years, macOS's resolver chain was
`.macos.<ext>` → `.<ext>`, silently missing the `.native.<ext>` step that iOS and Android both have. Any library
that ships a `.native.ts` file for "any real device, not web" fell through to its web/default implementation on
macOS instead — no crash, no warning. This section is the guard against the same one-line omission on Linux.

Metro's documented algorithm (`docs/research/codegen-and-oot-platform-tooling.md` §4.2) tries, for a given
`platform` and `sourceExts`, every source extension in turn, and for each extension the three suffixes
`.<platform>`, `.native`, then no suffix — it does **not** try every suffix across all extensions before moving
to the next extension. `resolvePlatformCandidates(moduleName, platform, sourceExts)` in
`packages/cli/src/metro-config.ts` encodes exactly that order as a pure function with no filesystem access:

```ts
resolvePlatformCandidates("Foo", "linux", ["ts", "js"]);
// → ["Foo.linux.ts", "Foo.native.ts", "Foo.ts", "Foo.linux.js", "Foo.native.js", "Foo.js"]
```

`resolveAgainstFilesystem(candidates, exists)` takes that ordered list and an injected `exists` predicate (Metro
itself would pass something backed by its own file map; the fixture tests below pass `node:fs`'s `existsSync`)
and returns the first candidate that exists, or `null` if none do. Neither function imports Metro or touches the
filesystem directly — per the Prime Directive, the resolver logic is a pure function a real `metro.config.js` can
call once Metro is actually wired in (issue #22); no Metro dependency is added by this change.

**Where this sits relative to the `src-linux` overlay:** `resolveLinuxOverlay` runs *before* the extension chain
and, when it matches, bypasses the chain entirely — it maps a fixed `react-native/<subpath>` straight to one
specific `src-linux` file that already has its own extension (e.g. `Platform.linux.ts`), because that subpath is
a permanent override, not a per-extension fallback. The extension chain only applies afterwards, to whatever
`moduleName` survives both the overlay check and the free package-name redirect (`react-native` →
`@react-native-linux/core`) — i.e. to ordinary third-party modules and to the small number of `@react-native-linux/core`
subpaths that are *not* in the overlay index. The full composed order for a single `resolveRequest` call is:

1. `resolveLinuxOverlay` — fixed-path override, short-circuits the chain.
2. `resolveLinuxPackageAlias` — the overlay packages of issue #180: an upstream package name, or a subpath of one,
   becomes `@react-native-linux/<lib>` when that package is registered in `packages/cli/src/package-aliases.ts`
   **and** resolves. The list ships empty, so today this step never fires. Steps 1 and 2 are the name-rewriting
   head of the chain and are composed as `resolveLinuxModuleName(moduleName, platform, isPackageResolvable)`; see
   `docs/ecosystem/package-template.md`.
3. `reactNativePlatformResolver`-style package-name redirect (free, from RN core).
4. `resolvePlatformCandidates` + `resolveAgainstFilesystem` — the `.linux` → `.native` → default chain, applied by
   Metro's own file resolver to whatever moduleName step 3 produced.

**Fixture proof** (`packages/cli/test-fixtures/resolution/`, exercised in `packages/cli/src/metro-config.spec.ts`):

| Fixture on disk | Candidates tried (`sourceExts: ["ts"]`) | Resolves to | Proves |
| --- | --- | --- | --- |
| `foo.linux.ts`, `foo.native.ts`, `foo.ts` | `foo.linux.ts`, `foo.native.ts`, `foo.ts` | `foo.linux.ts` | `.linux` wins over both `.native` and default |
| `bar.native.ts`, `bar.ts` (no `.linux`) | `bar.linux.ts`, `bar.native.ts`, `bar.ts` | `bar.native.ts` | `.native` wins over default when `.linux` is absent |
| `baz.ts` only | `baz.linux.ts`, `baz.native.ts`, `baz.ts` | `baz.ts` | default is used when neither `.linux` nor `.native` exist |
| `qux.web.ts` only | `qux.linux.ts`, `qux.native.ts`, `qux.ts` | `null` (unresolved) | `.web` is never a candidate for `linux`, even though a `.web` file exists on disk |
| `cycle.native.ts`, `cycle.linux.js` (`sourceExts: ["ts", "js"]`) | `cycle.linux.ts`, `cycle.native.ts`, `cycle.ts`, `cycle.linux.js`, `cycle.native.js`, `cycle.js` | `cycle.native.ts` | cycling is per-extension: `.ts` is exhausted (all three suffixes) before `.js` is tried at all, so `.native.ts` beats `.linux.js` |

The three-files-progressively-removed scenario from the issue's acceptance criteria is represented as three
separate fixtures (`foo`, `bar`, `baz`) rather than one fixture mutated between test runs, so the committed
fixture tree stays static and every assertion stays independent and order-independent.

## Reanimated bring-up rung: `.linux` → default, no `.native`, inside two packages

Tracks issue #178. The decision is #133's, recorded there verbatim: *"the JavaScript fallback is a bring-up rung
only — shipped internally so the flagship's Reanimated files run on Linux during development, keyed on our own Metro
resolver shim for linux, and deleted once Phase 2's native worklets runtime lands […]. Not a supported degraded
mode."* Animations on this rung run on the JavaScript thread, which ADR-0001 decision 6 promises they never will;
that promise is temporarily and explicitly untrue for as long as this shim exists, and the amendment recording it
rides with #135.

### The upstream rule, quoted

`react-native-windows` has no native Reanimated port; it runs the JavaScript (web) implementation, restored by
[software-mansion/react-native-reanimated#10117](https://github.com/software-mansion/react-native-reanimated/pull/10117),
merged 2026-09-02. Its own description states the rule in prose:

> This PR therefore extends `wrapWithReanimatedMetroConfig` so that internal relative imports from Reanimated and
> Worklets do not prefer `.native` files on Windows.

and in code, in `packages/react-native-reanimated/metro-config/index.js`:

```js
const WINDOWS_JS_FALLBACK_PACKAGE_ROOTS = [
  path.dirname(__dirname),
  path.dirname(require.resolve('react-native-worklets/package.json')),
];

function shouldUseWindowsJsFallback(context, moduleName, platform) {
  return (
    platform === 'windows' &&
    moduleName.startsWith('.') &&
    WINDOWS_JS_FALLBACK_PACKAGE_ROOTS.some((packageRoot) =>
      context.originModulePath.startsWith(`${packageRoot}${path.sep}`)
    )
  );
}
```

```js
// Keep `.windows` resolution, but skip unsupported `.native` modules.
const resolutionContext = shouldUseWindowsJsFallback(context, moduleName, platform)
  ? { ...context, preferNativePlatform: false }
  : context;
```

Three conditions, joined by `&&`: the active platform, a **relative** module name, and an origin module inside one
of the two package roots. `preferNativePlatform: false` is Metro's own switch for "do not try the `.native`
suffix"; it does not touch the platform suffix, which is what upstream's comment means by "Keep `.windows`
resolution".

### The same rule, keyed on `linux`

`shouldUseJavaScriptFallback(request)` in `packages/cli/src/metro-config.ts` reproduces those three conditions with
`windows` replaced by `linux`, and `resolveOriginAwareCandidates(request, sourceExts)` applies the consequence by
building the candidate list without the `.native` step:

```ts
resolveOriginAwareCandidates(
  {
    moduleName: "./WorkletsModule/NativeWorklets",
    originModulePath: "<...>/react-native-worklets/src/index.ts",
    platform: "linux",
  },
  ["ts", "js"],
);
// → ["<...>/WorkletsModule/NativeWorklets.linux.ts", "<...>/WorkletsModule/NativeWorklets.ts",
//    "<...>/WorkletsModule/NativeWorklets.linux.js", "<...>/WorkletsModule/NativeWorklets.js"]
```

Every other request — a different platform, a package (non-relative) import, or any origin outside those two
packages — falls through to `resolvePlatformCandidates` and keeps the ordinary `.linux` → `.native` → default chain
documented above. `resolveOriginAwareCandidates` is where the `resolveRequest` composition of #22 calls into the
chain; `resolveLinuxOverlay` still runs before it and still short-circuits.

One deliberate difference from upstream: we detect the two package roots by path **segment**
(`react-native-reanimated` or `react-native-worklets` appearing in `originModulePath`) rather than by
`require.resolve('<package>/package.json')`, because this repository depends on neither package and cannot resolve
them. The observable rule is the same for any layout where the package lives in a directory named after itself.

### Why the `.native` step has to go

Verified against the versions on disk in the flagship's install (`~/suuudokuuu/node_modules`), Reanimated `4.5.0`
and Worklets `0.10.0`:

- `react-native-worklets/src` ships **24** `.native.ts` files (`runtimes`, `threads`, all of `memory/**`,
  `WorkletsModule/NativeWorklets.native.ts`, …) beside unsuffixed siblings that are the JavaScript implementation.
  The published `lib/module/**` has the same split in `.js`. Metro treats `linux` as a native platform, so without
  the shim every one of those 24 resolves to the native file, which needs a Worklets JSI host that does not exist
  on Linux yet (#134–#138).
- `react-native-reanimated/src` ships exactly **one** `.native.ts`
  (`specs/SharedTransitionBoundaryProvider.native.ts`); Reanimated's own web/native split is a runtime branch, not
  a file suffix — see the next subsection.
- Both packages declare `"react-native": "src/index"`, so on a native platform Metro resolves their TypeScript
  sources and origin module paths land under `<package>/src/**`.

### What the shim does and does not reach, by version

This paragraph is version-specific, and the version we pin changed the answer.

**Reanimated 4.5.x** branched at runtime on `SHOULD_BE_USE_WEB = IS_JEST || IS_WEB || IS_WINDOWS`
(`src/common/constants/platform.ts`), consumed by 23 files — `export const makeMutable = SHOULD_BE_USE_WEB ?
makeMutableWeb : makeMutableNative` in `src/mutables.ts`, the `if (!SHOULD_BE_USE_WEB)` native initialisation in
`src/initializers.ts`, and so on. On `linux` that constant is `false`, so a file-suffix shim alone could not have
reached it, and the rung would have needed a patch.

**Reanimated 4.6.0 — the tag `packages/reanimated` pins — deleted that constant.** The runtime branch was replaced
by a file-suffix split: the tree went from 2 `.native.*` files at 4.5.0 to 38 at 4.6.0 (`mutables.native.ts`,
`ReanimatedModule/index.native.ts`, `useAnimatedStyle.native.ts`, …), and `grep SHOULD_BE_USE_WEB` over the
published 4.6.0 package returns nothing. `IS_WINDOWS` survives only as a `@knipIgnore`'d export with no consumers.
A file-suffix split is precisely what this shim steers, so at 4.6.0 the rung is the shim and nothing else — the
package's patch queue is empty for that reason.

`react-native-worklets` still carries its own split (24 `.native.ts` files), reached by the same rule via the
origin module path.

Upstream's own Windows fallback (`shouldUseWindowsJsFallback` in `metro-config/index.js`, PR #10117) merged after
4.6.0 was cut and first ships in 4.7.0. Ask 5 of the RFC in `docs/research/animation-on-linux.md` Appendix A (#145)
still stands: key that fallback on "this platform has no native Worklets module" rather than on a hardcoded
platform name, so the shim can be deleted rather than maintained.

### How the shim is exercised today

Unit only, against a committed fixture tree at `packages/cli/test-fixtures/reanimated-fallback/`, exercised in
`packages/cli/src/metro-config.spec.ts`. Fixture file names are kebab-cased to satisfy the repository's
`unicorn/filename-case` lint; the directory names are the real package names, which is what the rule keys on.

| Origin module | Request | Resolves to | Proves |
| --- | --- | --- | --- |
| `react-native-worklets/src/index.ts` | `./worklets-module/native-worklets` | `native-worklets.ts` | the `.native` sibling is skipped inside Worklets |
| `react-native-reanimated/src/index.ts` | `./hook/use-animated-style` | `use-animated-style.ts` | the same inside Reanimated |
| `react-native-reanimated/src/index.ts` | `./hook/use-shared-value` | `use-shared-value.linux.ts` | `.linux` still wins — upstream's "Keep `.windows` resolution" |
| `react-native-gesture-handler/src/index.ts` | `./gesture` | `gesture.native.ts` | every other package keeps preferring `.native` |
| `react-native-reanimated/src/index.ts` | `react-native-worklets` (package import) | `.linux` → `.native` → default | package imports are untouched, as upstream |
| `react-native-reanimated/src/index.ts` on `android` | `./hook/use-animated-style` | `.android` → `.native` → default | the rule is keyed on `linux`, nothing else |

### What waits for Metro: the runtime smoke bundle (#178 item 2)

Item 2 of #178 — a bundle proving `useSharedValue` + `withTiming` + `useAnimatedStyle` actually move a view on the
JS thread — is **not** in this repository yet, and is deliberately not faked. The e2e rig runs hand-written
bundles (`packages/core/test-bundles/*.js`, scenarios in `packages/core/e2e/*.json`); a hand-written bundle cannot
exercise Reanimated at all, because there is no Metro to run the resolver shim, no `react-native-reanimated`
dependency to bundle, and no Worklets Babel plugin (#137) to compile worklets. A hand-written stand-in would assert
our own JavaScript, not Reanimated's, and would prove nothing about the rung.

It unblocks when the app template and Metro wiring land (#22 for the resolver composition, #79/M3 for the dev
server that serves the bundle, #137 for the Babel plugin). At that point the smoke is a normal scenario —
`packages/core/e2e/reanimated-smoke.json` driving the bundled app — and the command is:

```bash
pnpm e2e --scenario reanimated-smoke
```

with the scenario's `expect` trace lines asserting the animated value progresses and its `frameBudget` recording
what the JS thread actually costs (the measurement #133's acceptance criteria ask for, "not an assumption").

### Startup warning and deletion trigger

`describeAnimationRung()` (`packages/cli/src/animation-rung.ts`, published as
`@react-native-linux/cli/animation-rung.ts`) returns the one-line warning the app template prints once at startup: *"Reanimated runs on the JavaScript thread on linux (bring-up rung, #178); native worklets:
#134–#138"*. It is a pure export with a unit test and no process-level side effects; the single call site arrives
with the template (#22).

**Deletion trigger.** The shim, the fixture tree, and `describeAnimationRung()` are deleted — not demoted to a
supported degraded mode — once the native worklets runtime replaces them: #136 (the `WorkletsModule` host, JSI
bindings, `requestAnimationFrame` on the FrameClock) landing green together with #138 (worklets conformance:
serialization, `runOnUI`/`runOnJS` round trips, runtime-per-thread). #133 states the same criterion as "#135/#136
green with the flagship's animations on the worklets runtime"; #135 (the UI scheduler) is #136's prerequisite, so
the trigger to watch is the pair #136/#138. If ask 5 of #145 is accepted upstream first, the shim is deleted
earlier and the rung continues on upstream's own capability check.

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
  no app package exists to hold one. `resolveLinuxOverlay`, `linuxOverlayIndex`, `resolvePlatformCandidates`,
  `resolveAgainstFilesystem`, `shouldUseJavaScriptFallback`, `resolveOriginAwareCandidates`,
  `resolveLinuxPackageAlias` and `resolveLinuxModuleName` are written and
  100%-tested as pure functions; composing them with
  `reactNativePlatformResolver` into an actual `resolver.resolveRequest` (including the origin-aware guard
  described above, honouring a user-supplied `resolver.resolveRequest` instead of replacing it, and adapting to
  Metro's real `CustomResolver`/`Resolution` types and its own `sourceExts` config, none of which are a dependency
  of this repo yet) is that issue's job, not this one's.
- **The remaining ~11 core JS overrides.** Land with the component/API issue that needs them (e.g. `BaseViewConfig`
  with the first Fabric component that needs prop parity), each adding one more `overrides.json` entry and one
  more `linuxOverlayIndex` key — not speculatively ahead of a real consumer.
- **The full-package vendoring step.** RNW's `generate.js`-style copy of the rest of upstream `Libraries/**` into
  `src-linux` before publish is not built. `@react-native-linux/core` is not yet a complete drop-in `react-native`
  replacement, and is not claimed to be one; see "What this is not" above.

