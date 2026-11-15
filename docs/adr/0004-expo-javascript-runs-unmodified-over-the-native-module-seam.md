# ADR-0004: Expo on Linux runs Expo's own JavaScript, unmodified, over the native-module seam

- Status: Accepted
- Date: 2026-11-15
- Deciders: Vitalii Yehorov
- Amends: ADR-0002, *Deferred, on the record* ("The Expo host")
- Research: `docs/research/ecosystem-compatibility.md` §3.4
- Issue: [#155](https://github.com/react-native-linux/react-native-linux/issues/155)

## Context

The flagship is now T3 Code's Expo app (epic [#634](https://github.com/react-native-linux/react-native-linux/issues/634),
Expo SDK 58). Its `apps/mobile/package.json` declares 37 `expo-*` packages besides `expo` and `@expo/ui`, and its
`src/` imports 30 distinct Expo modules; `expo-haptics` (23 import sites) and `expo-constants` (18) lead. It also
calls `requireNativeModule`, `requireOptionalNativeModule` and `requireNativeView` about 30 times for modules of its
own, and it uses `SharedObject` subclasses: `File`, `Directory` and `Paths` from `expo-file-system`, `expo-audio`'s
player and `expo-video`'s.

What the survey verified about Expo's native side, in `docs/research/ecosystem-compatibility.md` §3.4:

- Every Expo package checked ships `android/` (Kotlin) and `ios/` (Swift) and nothing else: no `common/cpp`, no
  `windows/`, no `macos/`.
- `expo-modules-core`'s 29 shared C++ files are a JSI object model (`SharedObject`, `SharedRef`, `EventEmitter`,
  `TypedArray`), not a module DSL. The `Module { Name(...); Function(...); View(...) }` DSL exists only in Kotlin
  and Swift.
- `expo-modules-autolinking` implements `apple`, `android`, `web` and `devtools`, and its `default:` throws.

What was verified about Expo's JavaScript side, in `expo-modules-core` 58:

- Its JavaScript reads one global, `globalThis.expo`, for the object model (`EventEmitter`, `NativeModule`,
  `SharedObject`, `SharedRef`) and for its module table. `EventEmitter.ts`, `NativeModule.ts` and
  `SharedObject.ts` read it as they evaluate, so without it the package throws on first use, and `expo` itself
  (`Expo.fx` imports `expo-asset`, which imports `expo-modules-core`) cannot start.
- The package publicly exports `./src/polyfill/dangerous-internal`, whose `installExpoGlobalPolyfill()` installs a
  pure-JavaScript `globalThis.expo`. Expo's web build installs it from `src/polyfill/index.web.ts`, and jest-expo
  calls it directly.
- `requireOptionalNativeModule(name)` looks in `globalThis.expo.modules`, then `NativeModulesProxy`, then
  `createTurboModuleToExpoProxy(TurboModuleRegistry.get(name))`, which copies a TurboModule's methods and its
  `getConstants()` onto a plain object. `requireNativeModule` is the same lookup, throwing
  `Cannot find native module '<name>'` instead of returning `null`.

The options were:

- **(a) A C++ Expo host.** Reuse `common/cpp`'s object model, write a C++ `ModuleDefinition` DSL, and reimplement
  each module's Kotlin body in C++.
- **(b) A JavaScript shim tier.** A Linux reimplementation of each module's JavaScript surface, swapped in for the
  upstream package by a package alias.
- **(c) The native-module seam.** Expo's own JavaScript, unchanged, over the pure-JavaScript `globalThis.expo` its
  web build uses, with this platform's TurboModules registered under Expo's native-module names.
- **(d) Neither.** Tell app authors to drop Expo, which is what `react-native-windows` effectively did.

## Decision

Option (c).

The reasons, in order of weight:

1. It runs the JavaScript Expo maintains. Every module's argument handling, defaults, errors, events and
   `SharedObject` classes stay upstream's, so an SDK bump changes nothing on Linux except where a native module's
   contract changes. (b) re-implements that JavaScript per module, per SDK.
2. The object model already exists, in JavaScript, maintained upstream for two consumers (web and jest-expo).
   `expo-file-system`'s `File` and `Directory` and the audio and video players subclass it unchanged.
3. What is left per module is the native module, which is the C++ TurboModule ADR-0002 already asks for. Every
   option needed it; (c) adds nothing on top.
4. It reaches modules no shim can: the application's own, through `requireNativeModule`, `requireOptionalNativeModule`
   and `requireNativeView`, which answer exactly as upstream answers for a module that is or is not there.

### The polyfill

One resolver rule installs `globalThis.expo`. `linuxPackageVariantIndex` in `packages/cli/src/metro-config.ts`
resolves `expo-modules-core/build/polyfill/index` to its `index.web` variant in a `linux` bundle, the same mechanism
`linuxUpstreamVariantIndex` uses for `react-native`'s own platform pairs. The polyfill then installs when
`expo-modules-core`'s index first evaluates, as on web, before any of its modules reads the global; no entry point
changes. Without the rule, the `expo-constants` e2e fails with upstream's own
`TypeError: Cannot read property 'EventEmitter' of undefined`.

The web polyfill's `modules` table starts empty, so lookups fall through to the TurboModule registry.
`reloadAppAsync`, `getViewConfig` and `installOnUIRuntime` keep the web answers (a `window.location` reload, which
this platform has no `window` for, and two throws) until a flagship module needs them.

### The module registry

A native module of ours is a C++ TurboModule registered in `packages/core/src/TurboModuleRegistry.cpp` under the
Expo native-module name (`ExponentConstants`, `ExpoHaptics`, `FileSystem`, `ExpoSQLite`, …). Upstream reaches it
through `createTurboModuleToExpoProxy`, so its constants come from `getConstants()`, its functions are its methods,
and an event is a method of that name taking the listener, which is what the proxy's `addListener` calls.

- **Optional lookup** (`requireOptionalNativeModule`) of a module this platform does not register returns `null`,
  and upstream's JavaScript takes its own "unavailable" path, as `expo-constants` does when it warns that
  `ExponentConstants` is not linked. Nothing of ours runs.
- **Required lookup** (`requireNativeModule`) of a module this platform does not register throws when the module is
  imported. A flagship module in that state is a missing native module, to be registered, aliased or declined
  below; it is never left to throw in a shipped app.

### Per module: the seam by default, an alias as the escape hatch

The seam is the default for every module. A package alias in `packages/cli/src/package-aliases.ts`, from the
upstream package to a subpath of `@react-native-linux/expo`, is the escape hatch for a module whose JavaScript
itself has to differ on Linux, not merely its native module. An escape-hatch alias leaves `<package>/package.json`
resolving to the upstream package, because tooling reads it. No module needs the escape hatch yet, so it has no code;
it lands with its first consumer, and the module's row in `packages/expo/README.md` says which way it went and why.

### The boundary: what a native module of ours answers

Each native module answers every constant and function its upstream module declares, in the module's own terms.
Silently doing nothing where the real module acts is the failure this boundary exists to prevent.

1. **Constants and build-time data**: answered, from the build or from the window.
2. **Capabilities the desktop has**: implemented. Linking and sharing go through `xdg-open` and the OpenURI portal,
   localization through the POSIX locale and the settings portal, the clipboard through #60, files through the XDG
   directories and the FileChooser portal, secure storage through the Secret Service, local notifications through
   `org.freedesktop.Notifications`, and digests through OpenSSL.
3. **Capabilities the desktop lacks**: the module's own documented mechanism. Where upstream has an availability
   query, it answers `false` (`Haptics.isAvailableAsync`, `ScreenCapture.isAvailableAsync`). Where it has none, the
   call rejects with upstream's `ERR_UNAVAILABLE` code. A whole module the desktop lacks is simply not registered,
   and upstream's optional lookup reports it.
4. **Native views** (`expo-image`, `expo-blur`, `expo-linear-gradient`, `expo-glass-effect`): one of our own Fabric
   components under the view name `requireNativeView` asks for. A view with no renderer counterpart is declined.

### The first module: `expo-constants`

`ExponentConstants` answers every field SDK 58's `NativeConstants` declares required, except the `name` upstream's
`Constants.ts` strips on every platform, for a bare desktop app: no status bar, the host name as the device name,
`debugMode` from the native build configuration as iOS reports it, `null` for the Expo Go and SDK versions and the
year class, empty experience and linking URIs, and a v4 session UUID per runtime. Upstream's `Constants.ts` runs
unchanged over it, proved by
`packages/core/tests/hermes/ExponentConstantsModuleTest.cpp` and the `expo-constants` e2e, which imports the real
package and also round-trips an event through `expo-modules-core`'s `EventEmitter`.
`packages/expo/src/expo-constants-surface.ts` lists upstream's required fields under a type that `pnpm ts` fails
when upstream adds one, and the e2e asserts each value.

`manifest` is `null`, so `Constants.expoConfig` is `null` and upstream warns, as it does in a bare Android app whose
config build script did not run. On Android and iOS the native module serves the app config the build embedded;
babel-preset-expo inlines `process.env.APP_MANIFEST` only for web and server bundles, and only upstream's web module
reads it. The Linux equivalent, the native module reading the app config a `linux` build embeds, is #156's next step.

### Declined, each with its substitute

- **`@expo/ui`.** SwiftUI and Jetpack Compose views have no Linux toolkit to host them, and hosting one would
  reverse ADR-0001's single paint model. Substitute: a core-component variant of each view the flagship uses, and
  `@react-native-masked-view/masked-view` (#164) for `@expo/ui/community/masked-view`.
- **`expo-updates`.** Updates on Linux belong to the distribution
  ([#360](https://github.com/react-native-linux/react-native-linux/issues/360)). Substitute: the distribution's
  package manager. Whether a native `ExpoUpdates` answers `isEnabled: false` or stays unregistered is #360's to
  decide.
- **`expo-dev-client`.** Its launcher and menu are native UI for choosing and reloading a development server.
  Substitute: the Metro dev server with this platform's Fast Refresh and reload (the `fast-refresh` and `reload`
  e2e scenarios) and React Native DevTools.
- **`expo-camera`.** Camera capture on Wayland goes through the PipeWire camera portal, and the flagship uses the
  camera only to scan a QR pairing code. Substitute: the flagship's QR pairing takes the pairing link typed or
  pasted, and `expo-image-picker` answers camera capture as unsupported (#275).

## Rejected

- **(b) A JavaScript shim tier.** This ADR first accepted it. It re-implements upstream's JavaScript for every
  module and every SDK, it cannot reach the application's own modules, and it does not avoid the object model:
  `expo-modules-core` throws without `globalThis.expo`, so the shim tier would have had to shim `expo-modules-core`
  too, which is rebuilding the object model upstream already ships for web.
- **(a) now.** It pays for a module DSL and a third implementation of every module before any module proves it needs
  more than the JavaScript object model.
- **(d).** The flagship declares 37 `expo-*` packages. Declining Expo declines the flagship.

## What would make us revisit

Any one of these reopens the question in a new ADR:

- **A flagship module whose JavaScript needs the native object model**: a `SharedObject` whose identity crosses the
  native boundary, a `SharedRef` handed to a native view, or worklets through `installOnUIRuntime`. That is the half
  of (a) that `common/cpp` already holds.
- **The polyfill moves or changes shape.** It is exported as `dangerous-internal`; an SDK that drops or reshapes it
  breaks the resolver rule, and the `expo-constants` e2e fails first.
- **Two modules that need the escape hatch for the same reason**, which would mean the seam lacks something general.
- **Expo ships a C++ module DSL**, so that (a) stops meaning that we write one.
- **A third-party Expo module whose maintainers will take a Linux implementation upstream.** That needs a native host
  they can target.

## Consequences

- #156 lands module by module: a C++ TurboModule under the Expo name, and an e2e that runs the real upstream package
  over it.
- `expo` itself (`registerRootComponent`, `Expo.fx`) runs over the same seam once the native modules it reaches are
  registered or optional; it is the next package to prove.
- `packages/expo` (`@react-native-linux/expo`) holds the per-module table, the e2e scenarios and the surface lists,
  and no runtime code until the first escape hatch. Its e2e installs the upstream packages; `pnpm-workspace.yaml`
  marks `expo-constants`' `expo` peer optional because its JavaScript imports only types from it.
- The upstream `expo-modules-autolinking` one-liner is still filed under
  [#160](https://github.com/react-native-linux/react-native-linux/issues/160), because discovery costs nothing and
  compounds. Autolinking keeps reporting an Expo module with no `linux` platform as not linked, which is expected:
  this platform's TurboModule registry, not autolinking, serves it.
