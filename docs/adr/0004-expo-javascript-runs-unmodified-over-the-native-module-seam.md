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

1. It runs the JavaScript Expo maintains. Every module's argument handling, defaults, errors and events stay
   upstream's, so an SDK bump changes nothing on Linux except where a native module's contract changes. (b)
   re-implements that JavaScript per module, per SDK.
2. The object model already exists, in JavaScript, maintained upstream for two consumers (web and jest-expo).
   It does not yet cover a module whose JavaScript subclasses a native class; see *Known gaps*.
3. What is left per module is the native module, which is the C++ TurboModule ADR-0002 already asks for. Every
   option needed it; (c) adds nothing on top.
4. It reaches modules no shim can: the application's own, through `requireNativeModule` and
   `requireOptionalNativeModule`, which answer exactly as upstream answers for a module that is or is not there.
   `requireNativeView` reaches them too, through the `getViewConfig` the platform answers (#662).

### The polyfill

One resolver rule installs `globalThis.expo`. `linuxPackageOverlayIndex` in `packages/cli/src/package-overlays.ts`
resolves `expo-modules-core/build/polyfill/index` in a `linux` bundle to
`packages/core/src-linux/expo-modules-core/polyfill/index.linux.ts`, which calls upstream's own
`installExpoGlobalPolyfill` (the package's public `src/polyfill/dangerous-internal` export, the call its `index.web`
variant makes) and then patches the global as below. It is the mechanism `linuxOverlayIndex` uses for
`react-native`'s own overridden files, and the overlay sits beside those in `@react-native-linux/core`, which declares
`expo-modules-core` an optional peer. `createLinuxResolveRequest` resolves the overlay's own imports from the file it
replaced, so `expo-modules-core` comes from the app's install as upstream's polyfill would, not from core's, where a
pnpm install holds no copy of it. The polyfill then installs when
`expo-modules-core`'s index first evaluates, as on web, before any of its modules reads the global; no entry point
changes. Without the rule, the `expo-constants` e2e fails with upstream's own
`TypeError: Cannot read property 'EventEmitter' of undefined`.

The web polyfill's `modules` table starts empty, so lookups fall through to the TurboModule registry. Its other
answers are web answers. `installExpoGlobalPolyfill` returns early when `globalThis.expo` already exists, so the
platform replaces one by patching the global after the polyfill installs, never by installing its own first. Each is
patched when its first consumer lands:

- `getViewConfig` throws `Method not implemented.`, which broke every native view. Patched (#662, below).
- `uuidv4` calls `crypto.randomUUID`, which this runtime lacks, and `cacheDir` and `documentsDir` are `undefined`
  (#664).
- `reloadAppAsync` reloads through `window.location`, which this platform has no `window` for, and
  `installOnUIRuntime` throws.

### Known gaps

Two shapes the flagship uses needed more than the seam above. Each is tracked, and each is claimed only as far as
its issue proved it.

1. **Native views: the first break. Resolved by
   [#662](https://github.com/react-native-linux/react-native-linux/issues/662).** In bridgeless mode
   `requireNativeView` (`requireNativeViewManager`) calls `NativeComponentRegistry.get`, whose view-config provider
   calls `globalThis.expo.getViewConfig`, and the web polyfill threw, so every `requireNativeView` crashed on first
   render: T3 Code's `FrostedCutout`, `SegmentedControl`, `NativePresentation` and `T3ComposerEditor`, and upstream's
   `expo-image` and `expo-blur`. The patched `getViewConfig` answers `null`, which is what upstream's native
   `getViewConfig` answers for a view that is not linked. Upstream's adapter warns
   `Unable to get the view config for <View> from module <Module>`, and the view mounts as an empty view that lays out
   and paints like a View with its children, after the `[component]` line `FabricHost` prints for any name with no
   component. Every Expo view takes this path today, since none is registered.
   - **Registered**: the registered answer, with real `validAttributes` and `directEventTypes` from a source that
     knows the props, lands with #165's first registered Expo view. An empty config would compose only the base View
     attributes and silently drop every prop of the view's own (`intensity`, `tint`, `source`), which this ADR exists
     to prevent.
   - Proved by `packages/core/src-linux/expo-modules-core/polyfill/index.linux.spec.ts` and the `expo-native-view`
     e2e, which renders T3 Code's `FrostedCutout` call through upstream's unmodified `requireNativeViewManager` and
     compares the frame, the child painted inside the fallback view, against a checked-in golden.
2. **Constructor-backed modules: proved for `expo-file-system`.** `expo-file-system`'s `File.ts` declares
   `class File extends ExpoFileSystem.FileSystemFile` and passes its instances back into native code;
   `expo-audio`'s `AudioPlayer` and `expo-video`'s players follow the same pattern. `createTurboModuleToExpoProxy`
   copies only a TurboModule's prototype methods and `getConstants()` values, and the polyfill's
   `SharedObject.release()` is a no-op, so the module has to supply both the class and the release
   ([#663](https://github.com/react-native-linux/react-native-linux/issues/663)). It does, with
   `makeSharedObjectClass` in `packages/core/src/ExpoSharedObject.h`:
   - `getConstants()` returns a constructor that extends `globalThis.expo.SharedObject`, read when the module is
     first required, after `expo-modules-core` installed the polyfill. Hermes cannot `new` a host function, so the
     constructor is a small evaluated JavaScript function, as #665's `ExpoSQLite` builds `NativeDatabase`; it builds the
     instance from `new.target`, so a subclass's `super(...)` gets the subclass's prototype.
   - Each instance carries a JSI native state, and a native method resolves an instance passed back to it by that
     state, so identity crosses the boundary without a table the module owns.
   - `release()` frees the state at once; an instance never released frees it when Hermes collects it. Hermes keeps
     a replaced native state until it collects the object, so the native state is a handle whose state `release()`
     resets, rather than a state that `release()` replaces.

   `packages/core/tests/hermes/ExpoSharedObjectTest.cpp` is the constructor-and-release probe, and the `FileSystem`
   module (`packages/core/src/ExpoFileSystemModule.cpp`) is its first consumer: upstream's `File`, `Directory` and
   `Paths`, unmodified, write, read, list, copy, move and delete in the XDG directories in the `expo-file-system` e2e.
   Every instance's state stays on the JavaScript thread: an asynchronous method copies what it needs into native
   values, does its file work on a worker, and settles back on the JavaScript thread.

   One thing an app supplies, not the module: a WHATWG `URL`. Upstream's `Paths.join` assigns `pathname` on a `URL`,
   which React Native's own `URL` silently ignores, so `new File(Paths.document, 'name')` names the document
   directory itself. `expo`'s runtime (`expo/src/winter`) installs `whatwg-url-minimum`'s `URL` in every app that
   imports `expo`, as the flagship does; the e2e, which does not import `expo`, installs the same `URL` itself.
   `expo-audio` and `expo-video` reuse `makeSharedObjectClass` when they land, and so does `expo`'s own
   `ExpoFetchModule`: `expo/fetch` constructs its `NativeRequest` and extends its `NativeResponse`, and `Expo.fx`
   installs that `fetch` as the global one. Until it lands
   ([#673](https://github.com/react-native-linux/react-native-linux/issues/673)), the escape hatch below keeps React
   Native's own `fetch`.

### The module registry

A native module of ours is a C++ TurboModule registered under the Expo native-module name (`ExponentConstants`,
`ExpoHaptics`, `FileSystem`, `ExpoSQLite`, …). Only its factory line goes in `packages/core/src/TurboModuleRegistry.cpp`;
from the second Expo module on, each module's class lives in its own `packages/core/src/Expo<Name>Module.cpp`, so
parallel work on two modules does not collide in one file. Upstream reaches it through `createTurboModuleToExpoProxy`,
so its constants come from `getConstants()` and its functions are its methods. A module that needs more than its
instances, a worker thread or the application directory, gets an object the registry owns and `WindowSession`
configures before the bundle runs: `ExpoSQLite`'s `SQLiteDatabases` and `FileSystem`'s `FileSystemHost`. The registry
destroys it only after the JavaScript thread has quit, so a method can hold it by reference.

An event is a method of the event's name that takes the listener, which is what the proxy's `addListener` calls, and
it returns an `EventSubscription`, the shape of React Native's C++ event emitter. The proxy has no `removeListener`,
`removeAllListeners` or `emit`, so removal goes through that subscription. The `EventEmitter` round trip in the
`expo-constants` e2e proves only that the polyfill is installed, not native event delivery. `ExpoLinking`'s
`onURLReceived` subscribes to the `url` device event an activation already emits, proved from a native activation to
the listener by `ExpoStartupModulesTest`; `ExpoNetwork`'s `onNetworkStateChanged` is React Native's C++ event emitter
and emits nothing yet. The first module that emits on its own emitter adds an e2e that delivers one from native code.

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
resolving to the upstream package, because tooling reads it. The module's row in `packages/expo/README.md` says which
way it went and why.

The first escape hatch is a stopgap rather than a module whose JavaScript must differ: `expo`'s `fetch`, whose native
module is #673's, and without which every application that imports `expo` loses the global `fetch` at its first
call. Upstream reaches the file relatively (`Expo.fx`'s runtime calls `require('./fetch')`), which no package alias
sees, so `linuxPackageSubstituteIndex` in `packages/cli/src/package-overlays.ts` resolves the resolved file
`expo/build/winter/fetch/index` again as `@react-native-linux/expo/fetch`, React Native's own `fetch`, once the
application installs that package. #673 removes it. Upstream's own switch, `EXPO_PUBLIC_USE_RN_FETCH`, is not used:
it is inlined from the Metro process's environment, so one dev server bundling for Linux, iOS and Android would
switch all three.

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
   components registered under the host name `requireNativeView` asks for, `ViewManagerAdapter_<Module>_<View>`,
   which `getViewConfig` then answers (#662). A view with no renderer counterpart is declined.

### The first module: `expo-constants`

`ExponentConstants` answers every field SDK 58's `NativeConstants` declares required, except the `name` upstream's
`Constants.ts` strips on every platform, for a bare desktop app: no status bar, the host name as the device name,
`debugMode` from the native build configuration as iOS reports it, `null` for the Expo Go and SDK versions and the
year class, empty experience and linking URIs, and a v4 session UUID per runtime. Upstream's `Constants.ts` runs
unchanged over it, proved by
`packages/core/tests/hermes/ExponentConstantsModuleTest.cpp` and the `expo-constants` e2e, which imports the real
package and also round-trips an event through `expo-modules-core`'s JavaScript `EventEmitter`, which proves only
that the polyfill is installed.
`packages/expo/src/expo-constants-surface.ts` lists upstream's required fields under a type that `pnpm ts` fails
when upstream adds one, and the e2e asserts each value.

`manifest` is the app config the build embedded, as on Android and iOS. Android's Gradle step runs upstream's
`expo-constants/scripts/getAppConfig.js`, which evaluates `app.json` or `app.config.*` with `@expo/config` into the
APK's `assets/app.config`, and the native module serves that file as a JSON string that `Constants.ts` parses.
babel-preset-expo inlines `process.env.APP_MANIFEST` only for web and server bundles, and only upstream's web module
reads it. A `linux` build runs the same script into `assets/app.config` beside its bundle, the directory #70 already
reads `assets/fonts` from, and `ExponentConstants` serves that file. The e2e harness runs it for a scenario package with an
`app.json` (`scripts/window-golden.ts`), and the `expo-constants` e2e asserts `expoConfig.scheme` and
`Linking.createURL`; the CLI build, and a config source for a bundle Metro serves, is
[#696](https://github.com/react-native-linux/react-native-linux/issues/696). A bundle Metro serves has no directory beside it, so its `manifest` stays `null`, and
upstream warns, as it does in a bare Android app whose config build script did not run.

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
  native boundary that `makeSharedObjectClass` cannot carry (#663 carries `expo-file-system`'s), a `SharedRef`
  handed to a native view, or worklets through `installOnUIRuntime`. That is the half of (a) that `common/cpp`
  already holds.
- **The polyfill moves or changes shape.** The resolver rule keys on deep paths: `build/index.js` importing
  `./polyfill`, `build/polyfill/index.js`, and the public `src/polyfill/dangerous-internal` export with its
  `installExpoGlobalPolyfill`. An SDK that moves or reshapes them breaks the rule, and the `expo-constants` e2e fails
  first; one that changes how `requireNativeView` builds its host name or calls `getViewConfig` fails the
  `expo-native-view` e2e.
- **Two modules that need the escape hatch for the same reason**, which would mean the seam lacks something general.
- **Expo ships a C++ module DSL**, so that (a) stops meaning that we write one.
- **A third-party Expo module whose maintainers will take a Linux implementation upstream.** That needs a native host
  they can target.

## Consequences

- #156 lands module by module: a C++ TurboModule under the Expo name, and an e2e that runs the real upstream package
  over it.
- `expo` itself (`registerRootComponent`, `Expo.fx`) runs over the same seam, proved by the `expo-entry` e2e. Of the
  native modules it reaches as it loads, `ExponentConstants` and `ExpoAsset` are registered, `ExpoGo`, `ExpoUpdates`
  and `EXDevLauncher` are optional lookups that answer as a bare app, and `ExpoFetchModule` is the escape hatch above.
  A development bundle also reaches `ExpoKeepAwake`, through a lookup upstream catches. The e2e bundles through this
  platform's Metro config and Babel preset; the same entry bundled through `expo/metro-config` and
  `babel-preset-expo`, as an Expo application bundles, is
  [#687](https://github.com/react-native-linux/react-native-linux/issues/687).
- T3 Code's startup graph reaches more Expo modules through `requireNativeModule`; `docs/research/t3code-linux.md`
  §1 lists each one, whether it is registered, and the issue that owns it. `ExpoApplication`, `ExpoDevice`,
  `ExpoLinking` and `ExpoNetwork` are registered, proved by the `expo-startup-modules` e2e.
  `ExpoFontLoader` registers a font file `Font.loadAsync` or `useFonts` downloaded into the text pipeline's application
  fonts (#70), the same registry `assets/fonts` fills, proved by the `expo-font` e2e and its screenshot golden.
- `packages/expo` (`@react-native-linux/expo`) holds the per-module table, the e2e scenarios, the surface lists and
  the escape hatches' runtime code. Its e2e installs the upstream packages; `pnpm-workspace.yaml`
  marks `expo-constants`' `expo` peer optional because its JavaScript imports only types from it.
- The upstream `expo-modules-autolinking` one-liner is still filed under
  [#160](https://github.com/react-native-linux/react-native-linux/issues/160), because discovery costs nothing and
  compounds. Autolinking keeps reporting an Expo module with no `linux` platform as not linked, which is expected:
  this platform's TurboModule registry, not autolinking, serves it.
