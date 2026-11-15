# ADR-0004: Expo modules on Linux are a JavaScript shim tier over our own TurboModules

- Status: Accepted
- Date: 2026-11-15
- Deciders: Vitalii Yehorov
- Amends: ADR-0002, *Deferred, on the record* ("The Expo host")
- Research: `docs/research/ecosystem-compatibility.md` §3.4
- Issue: [#155](https://github.com/react-native-linux/react-native-linux/issues/155)

## Context

The flagship is now T3 Code's Expo app (epic [#634](https://github.com/react-native-linux/react-native-linux/issues/634),
Expo SDK 58). Its `apps/mobile/package.json` declares 37 `expo-*` packages besides `expo` and `@expo/ui`, and its
`src/` imports 30 distinct Expo modules; `expo-haptics` (23 import sites) and `expo-constants` (18) lead. No other
decision in M4 reaches as many dependencies.

What the survey verified about Expo modules, in `docs/research/ecosystem-compatibility.md` §3.4:

- Every Expo package checked ships `android/` (Kotlin) and `ios/` (Swift) and nothing else: no `common/cpp`, no
  `windows/`, no `macos/`.
- `expo-modules-core` is 238 Kotlin and 181 Swift files against 29 shared C++ files, and that C++ is a JSI object
  model (`SharedObject`, `SharedRef`, `EventEmitter`, `TypedArray`, `fabric/ExpoView*`), not a module DSL. The
  `Module { Name(...); Function(...); View(...) }` DSL exists only in Kotlin and Swift.
- `expo-modules-autolinking`'s `getLinkingImplementationForPlatform` implements `apple`, `android`, `web` and
  `devtools`, and its `default:` throws. `getSupportPackageForPlatform` already has a `windows` case with no linking
  implementation behind it.
- Neither `expo-sqlite` nor `expo-updates` has portable C++: their C++ is fbjni-bound, under `android/src/main/cpp`.

The options were:

- **(a) A C++ Expo host.** Reuse `common/cpp`'s object model, write a C++ `ModuleDefinition` DSL, and reimplement
  each module's Kotlin body in C++. Highest fidelity and highest cost, and it forks a target that moves every SDK.
- **(b) A JavaScript shim tier.** A Linux implementation of each module's JavaScript surface, over a few C++
  TurboModules of our own where the module needs the operating system. `lucid-softworks/react-native-linux` ships
  26 such shims (ADR-0001), the only evidence in the wild either way.
- **(c) Neither.** Tell app authors to drop Expo, which is what `react-native-windows` effectively did.
  rn-windows#13534 ("Expo support") is still its most-reacted open issue.

## Decision

Option (b). Option (a) is reserved for a module that the flagship proves needs it, under the triggers below.

The reasons, in order of weight:

1. There is nothing to reuse for (a). Expo's shared C++ is an object model without a DSL, so a C++ host means
   writing the DSL and then every module body: a third implementation of each module, maintained against an SDK that
   changes every release.
2. The modules (b) cannot reach (`expo-sqlite`, `expo-image`, `expo-notifications`) need a C++ TurboModule of ours
   in either option. A system SQLite, the core image pipeline and `org.freedesktop.Notifications` are platform
   services, and (a) would wrap the same TurboModules in a second module system.
3. Epic #634 needs Expo modules now. (b) reaches the flagship's thin tier, the eleven modules of
   [#156](https://github.com/react-native-linux/react-native-linux/issues/156), with no new module system.

### How a shim is delivered

- One workspace package, `packages/expo`, published as `@react-native-linux/expo`, with one
  `src/<upstream-name>.linux.ts` per module and a subpath export named after the upstream package
  (`@react-native-linux/expo/expo-constants`).
- A package alias from the upstream name to that subpath, in `packages/cli/src/package-aliases.ts`. This is the
  name-rewriting head of the resolver chain that #55 and #180 landed, so it rewrites `expo-constants` and all of its
  subpaths in a `linux` bundle, and only when the application has installed `@react-native-linux/expo`. iOS,
  Android and web bundles never see the alias.
- The upstream package stays installed. It supplies the TypeScript types the application compiles against and the
  native code the other platforms link. The shim supplies only the runtime values of a `linux` bundle.
- A shim that needs the operating system calls a C++ TurboModule registered by this platform, the same kind every
  core module is. It does not install `globalThis.expo` or answer `requireNativeModule`.

The first shim is `expo-constants`, proved by `packages/expo/src/expo-constants.linux.spec.ts` and the
`expo-constants` e2e scenario. Its app config is read from `process.env.APP_MANIFEST`, the build-time contract
that upstream's own web implementation reads. Inlining that variable into a `linux` bundle from `expo/config` is
#156's next step. Until then `Constants.expoConfig` is `null`, which is what upstream answers in a bare app whose
config build script did not run.

### The boundary: what a shim may implement, and what must say no

A shim exports every runtime name its upstream module exports, and each name either works or says no in the
module's own terms. Silently doing nothing where the real module acts is the failure this boundary exists to
prevent, because it produces "library X is broken on Linux" reports that nobody can reproduce.

1. **Constants and build-time data**, such as `expo-constants`, the app identity in `expo-application`, and the
   splash policy: implemented, from the bundle or from the window.
2. **Capabilities the desktop has**: implemented over a TurboModule of ours. Linking and sharing go through
   `xdg-open` and the OpenURI portal, localization through the POSIX locale and the settings portal, the clipboard
   through #60, files through the XDG directories and the FileChooser portal, secure storage through the Secret
   Service, local notifications through `org.freedesktop.Notifications`, and digests through OpenSSL.
3. **Capabilities the desktop lacks**: report it through the module's own documented mechanism. Where upstream
   has an availability query, it answers `false` (`Haptics.isAvailableAsync`, `ScreenCapture.isAvailableAsync`).
   Where it has none, the call rejects with upstream's `ERR_UNAVAILABLE` code and the message expo-modules-core's
   `UnavailabilityError` uses. A purely presentational call that upstream already ignores on another platform, such
   as the status bar style or the glass effect on Android, may do nothing, as it does there.
4. **Native views** (`expo-image`, `expo-blur`, `expo-linear-gradient`, `expo-glass-effect`): the shim renders one
   of our own Fabric components, or a core component where that is the whole effect. A view with no renderer
   counterpart is declined, below.

Each shim's README row states which surfaces are implemented and which say no, so #87's matrix can distinguish
`shimmed` from `works`.

### Declined, each with its substitute

- **`@expo/ui`.** SwiftUI and Jetpack Compose views have no Linux toolkit to host them, and hosting one would
  reverse ADR-0001's single paint model. Substitute: a core-component variant of each view the flagship uses, and
  `@react-native-masked-view/masked-view` (#164) for `@expo/ui/community/masked-view`.
- **`expo-updates`.** Updates on Linux belong to the distribution
  ([#360](https://github.com/react-native-linux/react-native-linux/issues/360)). Substitute: the distribution's
  package manager. What the shim answers to `Updates.isEnabled` and to a manual check is #360's to decide.
- **`expo-dev-client`.** Its launcher and menu are native UI for choosing and reloading a development server.
  Substitute: the Metro dev server with this platform's Fast Refresh and reload (the `fast-refresh` and `reload`
  e2e scenarios) and React Native DevTools.
- **`expo-camera`.** Camera capture on Wayland goes through the PipeWire camera portal, and the flagship uses the
  camera only to scan a QR pairing code. Substitute: the flagship's QR pairing takes the pairing link typed or pasted, and
  `expo-image-picker` answers camera capture as unsupported (#275).

## Rejected

- **(a) now.** It pays for a module DSL and a third implementation of every module before any module proves it
  needs that fidelity.
- **(c).** Nineteen of the previous flagship's thirty native dependencies were Expo modules, and the current
  flagship declares 37 `expo-*` packages. Declining Expo declines the flagship.
- **Shimming at the native-module seam instead of the JavaScript surface.** Installing `globalThis.expo` with
  expo-modules-core's JavaScript object model, the one its web build uses, would let upstream's own JavaScript run
  unchanged over JavaScript-implemented native modules. It is a module host in all but language, and it makes
  every Linux bundle depend on expo-modules-core's native code path evaluating without its JSI installer, which
  nothing has proved. It is the first thing to re-examine if the triggers below fire.

## What would make us revisit (a)

Any one of these reopens the question in a new ADR:

- **Two flagship modules whose JavaScript surface is built on native `SharedObject` classes**, such that each shim
  rebuilds the same object model. `expo-audio`'s `AudioPlayer`, `expo-video`'s `VideoPlayer`, and the `File` and
  `Directory` classes of `expo-file-system` are the candidates. Two is the Prime Directive's second consumer.
- **A shim that has to copy upstream's JavaScript rather than call a TurboModule**, because the logic lives in the
  JavaScript layer (`expo-sqlite`'s statement and transaction API, `expo-notifications`' handler plumbing) and
  re-implementing it costs more than hosting it.
- **Expo ships a C++ module DSL**, so that (a) stops meaning that we write one.
- **A third-party Expo module the flagship needs whose maintainers will take a Linux implementation upstream.** That
  needs a host they can target, and a shim inside our package is not one.

## Consequences

- #156, the eleven-module thin tier, is unblocked and lands in `packages/expo` one module at a time.
- `expo` itself, for `registerRootComponent`, and the expo-modules-core helpers that third-party Expo modules import
  (`CodedError`, `UnavailabilityError`, `EventEmitter`) are shims in the same package, so T3 Code's `index.ts` starts
  without the Expo native runtime.
- The upstream `expo-modules-autolinking` one-liner is still filed under
  [#160](https://github.com/react-native-linux/react-native-linux/issues/160), because discovery costs nothing and
  compounds. Autolinking keeps reporting an Expo module with no `linux` platform as not linked, which is now
  expected: the shim tier, not autolinking, serves it.
- Every shim tracks an SDK. A shim pins the upstream major it was written against in its README row, and #156's
  surface test fails when an upstream export appears that the shim lacks.
