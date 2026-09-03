# Ecosystem compatibility: a C++-first layer, and the library survey that sizes it

- Research report, 2026-09-03. Written for epic #96, milestone M4 (ADR-0001's "ecosystem porting programme").
- Verification legend: **[V]** verified by reading the primary source on this machine or through `gh api` /
  the npm registry, **[I]** inferred from what was read, **[?]** not confirmed.
- The machine-readable plan this document explains is `scripts/issue-plans/ecosystem.json`: **25 proposed
  issues** — fifteen for the layer, its two deferred decisions and its documentation, then ten ports and
  verifications drawn from the ranking in §7. They are sub-issues of #96, not of #1.
- Reanimated, `react-native-worklets` and the `Animated` native driver are **out of scope here**. They are
  owned by `docs/research/animation-on-linux.md` and planning issue #95. This report classifies them in the
  survey table for completeness and defers every decision about them to that programme.

## Sources and versions

| Artifact | Version | Where it was read |
| --- | --- | --- |
| `react-native` (vendored) | **0.87.1** | `third_party/react-native/packages/react-native`, pinned by `scripts/vendor.lock.json` [V] |
| `@react-native/codegen` | 0.87.1 | `node_modules/@react-native/codegen/lib` [V] |
| Suuudokuuu (flagship) | **2.12.1** | `~/.t3/worktrees/suuudokuuu/t3code-c2e11773/packages/app` — the newest worktree, 2026-09-03 [V] |
| Suuudokuuu `node_modules` | resolved for 2.9.4 | `~/suuudokuuu/node_modules` — the only installed tree on this machine [V] |
| npm download counts | last month, window `2026-07-31` → `2026-08-29` | `https://api.npmjs.org/downloads/point/last-month/<pkg>` [V] |
| `react-native-nitro-modules` | 0.36.1 | `~/suuudokuuu/node_modules/react-native-nitro-modules` [V] |
| `nitrogen` | 0.37.1 | `npm view nitrogen` [V] |
| `expo-modules-core` | 57.0.3 | `~/suuudokuuu/node_modules/expo-modules-core` [V] |
| `expo-modules-autolinking` | bundled with SDK 57 | `~/suuudokuuu/node_modules/expo-modules-autolinking/src` [V] |
| `create-react-native-library` | 0.63.0 | `callstack/react-native-builder-bob` via `gh api` [V] |
| Tencent MMKV | tip of `main` | `gh api repos/Tencent/MMKV/contents` [V] |

---

## 0. Headline answers

1. **Of the fifty most-downloaded React Native libraries that ship native code, exactly one runs on Linux
   today with zero Linux-specific code — and it does so because it has no native code left.**
   `@shopify/flash-list` 2.3.2 is 100% TypeScript; its only `.kt`/`.swift` files are in its own example
   fixture app [V]. **Zero** of the fifty are portable C++ TurboModules or C++-implemented Nitro modules that
   would link and run unchanged. The C++-first layer is not a shortcut into the existing ecosystem. It is the
   thing that makes each port small, and the thing that makes *new* libraries free.
2. **The nearest miss proves the strategy.** `react-native-mmkv` 4.x implements its entire storage hybrid
   object in portable C++ (`packages/react-native-mmkv/cpp/HybridMMKV.cpp`), declares it in `nitro.json` as
   `"MMKVFactory": { "all": { "language": "c++" } }`, and its dependency — Tencent MMKV — ships a POSIX port
   with its own `CMakeLists.txt` [V]. What stands between it and Linux is **three files in Nitro core**, not
   anything in mmkv.
3. **Nitro is the highest-leverage integration in the entire survey.** `react-native-nitro-modules`' `cpp/`
   tree has exactly **two** `#ifdef ANDROID` conditionals and **no** fbjni or Objective-C includes [V]. Its
   platform surface is a declared contract: `cpp/platform/ThreadUtils.hpp` (four static methods),
   `cpp/platform/NitroLogger.hpp` (one `nativeLog`), and a `Dispatcher` subclass for the UI thread. Implement
   those and every Nitro library's C++ half works; `nitro.json`'s `autolinking` block then tells us, per
   library and machine-readably, exactly which hybrid objects still need a Linux implementation.
4. **Expo modules are their own module system, and it has no C++ author surface at all.** Every one of the
   ~35 Expo packages checked ships `android/` (Kotlin) and `ios/` (Swift) and nothing else — no `common/cpp`,
   no `windows/`, no `macos/` directory [V]. `expo-modules-core` itself is 238 Kotlin files and 181 Swift
   files against 29 files of shared C++, and that C++ is a JSI object model (`SharedObject`, `SharedRef`,
   `EventEmitter`, `TypedArray`, `ExpoView*`), not a module DSL. `expo-modules-autolinking`'s
   `getLinkingImplementationForPlatform` has implementations for exactly `apple`, `android`, `web` and
   `devtools`, and throws on anything else [V]. Supporting Expo on Linux means writing a third host for the
   module DSL, in C++, and then reimplementing each module's Kotlin/Swift body. There is no version of this
   that is cheap. **This is the single largest finding in the report** — the flagship depends on twenty Expo
   packages.
5. **For Fabric components the C++ half is already portable and the port is only the view half — which, on a
   canvas platform, is a paint routine.** `react-native-safe-area-context`, `react-native-screens`,
   `react-native-svg`, `react-native-keyboard-controller`, `@react-native-community/slider` and
   `@react-native-picker/picker` all ship `common/cpp/react/renderer/components/**` with real
   `ShadowNode`, `State` and `ComponentDescriptor` sources [V]. Autolinking those descriptors into
   `FabricHost::registerComponentDescriptors` is mechanical. What is not mechanical is the mounted view — and
   for a masked view, a blur, a gradient or an SVG path, "the mounted view" is a Skia draw call we would have
   had to write anyway.
6. **The `cpp-library` scaffold every author already uses is one file away from working on Linux.**
   `create-react-native-library` 0.63's `cpp-library` template puts the logic in `cpp/`, and its Android
   `CMakeLists.txt` links exactly three targets: `jsi`, `reactnative`, and `react_codegen_<Name>Spec` [V].
   Export those three names from `packages/core` and that CMakeLists builds on Linux verbatim. The only
   genuinely missing piece is registration: the template's `ios/OnLoad.mm` uses ObjC `+load` and Android uses
   `JNI_OnLoad` to call `registerCxxModuleToGlobalModuleMap`. We generate that call instead of asking authors
   to write a third one.
7. **Emulating a foreign native ABI is rejected, and the survey is the reason.** Even if a macOS ObjC runtime
   or an Android JNI shim existed on Linux, the libraries that matter link Foundation, UIKit, AppKit,
   `androidx.*`, Google Play services, AVFoundation and Metal. The binaries are not portable, so the ABI is
   not the binding constraint. §6 records this properly.
8. **The flagship is gated on Expo, not on C++.** Suuudokuuu declares **30** direct dependencies that ship
   native code, and **19 of them are Expo modules** (20 counting `expo-modules-core`, which every one of them
   sits on) — matching ADR-0001's "roughly thirty … about twenty Expo modules". Three are Fabric component
   libraries with portable shadow nodes, two are Nitro, and one — `react-native-gesture-handler` — has no
   shared C++ whatsoever. A C++-first layer does not run Suuudokuuu. The layer plus ten ports does.

---

## 1. The flagship's native dependency ledger

Read from `~/.t3/worktrees/suuudokuuu/t3code-c2e11773/packages/app/package.json` (v2.12.1) and the resolved
tree in `~/suuudokuuu/node_modules` [V]. Import counts are from `packages/app/src` [V]; a dependency with no
direct imports is reached transitively (through `@suuudokuuu/screen-chrome`, `@suuudokuuu/ui`, `expo-router`
or `lucide-react-native`).

Port size: **S** ≈ one contained subsystem, days. **M** ≈ a subsystem plus a Linux service integration,
weeks. **L** ≈ a full third implementation of the library's platform half.

| Package | Direct imports | Native surface | Evidence | macOS | Windows | Renderer? | Linux path |
| --- | ---: | --- | --- | --- | --- | --- | --- |
| `react-native-unistyles` 3.3.0 | **96** | Nitro; portable `cxx/` core (43 files: registry, parser, shadow-tree commit) + a Swift/Kotlin `NativePlatform` hybrid | `cxx/core`, `nitrogen/generated/shared/c++`, `ios/NativePlatform+ios.swift` | no | no | yes (commits to the shadow tree) | **port S/M** — implement 18 pure-virtual methods of the generated `HybridNativePlatformSpec` |
| `expo-router` 57.0.17 | 45 | Expo module; 15 Swift files, 1 Kotlin (link preview), rest JS | `expo-router/ios`, `expo-router/android` | no | no | no | **port S** once `react-native-screens` and `expo-linking` exist |
| `react-native-reanimated` 4.5.0 | 27 | 235 portable C++ files + 38 Apple / 29 Kotlin platform files; 87 `ANDROID`/`__APPLE__` conditionals in `Common/cpp` | `Common/cpp/reanimated`, `apple/NativeProxy.mm`, `Common/cpp/reanimated/Tools/PlatformDepMethodsHolder.h` | reuses `apple/` | JS fallback only | yes | **owned by #95** |
| `react-native-safe-area-context` 5.7.0 | 10 | Fabric C++ shadow node + state (5 files) + 29 ObjC / 23 Kotlin view files | `common/cpp/react/renderer/components/safeareacontext` | podspec `osx 10.15` | no | yes | **port S** — insets are a compositor fact; shadow node is already ours |
| `expo-haptics` 57.0.2 | 9 | Expo module; 7 Kotlin, 1 Swift | `expo-haptics/android`, `/ios` | no | no | no | **port S** — no haptics on a desktop; a truthful no-op with `isAvailableAsync() === false` |
| `react-native-gesture-handler` 2.32.0 | 5 | **Zero shared C++.** 62 Kotlin + 57 Apple files; the orchestrator and every recogniser exist twice | `android/src/main/java/.../GestureHandlerOrchestrator.kt`, `apple/Handlers` | podspec `osx 10.15` | no | yes (hit testing, touch capture) | **port L** — a third implementation of the state machine |
| `@expo/ui` 57.0.14 | 4 | Expo module wrapping SwiftUI (136 files) and Jetpack Compose (73 files) | `@expo/ui/ios`, `/android` | no | no | yes | **decline** — flagship uses `community/bottom-sheet`; substitute is our own sheet |
| `@shopify/react-native-skia` 2.6.2 | 3 | 747 portable C++ files (`cpp/api`, `cpp/jsi`, `cpp/rnskia`) + a per-platform `PlatformContext`/`WindowContext`; ships **prebuilt Skia** in `libs/{android,ios,macos,tvos}` | `cpp/rnskia/RNSkPlatformContext.h`, `apple/MetalWindowContext.mm`, `android/cpp/rnskia-android/OpenGLWindowContext.cpp` | yes (`libs/macos`) | no | **yes, decisively** | **port L + `needs:decision`** — two Skia builds in one process (ADR-0001 records this) |
| `expo-sharing` 57.0.16 | 3 | Expo module; 11 Kotlin, 7 Swift | `expo-sharing/android` | no | no | no | **port S** — `xdg-desktop-portal` `org.freedesktop.portal.OpenURI` |
| `lucide-react-native` 1.38.0 | 3 (+ ~30 icon paths) | none — pure JS over `react-native-svg` | `package.json` peer | n/a | n/a | via SVG | **works once `react-native-svg` works** |
| `react-native-worklets` 0.10.0 | 2 | 76 portable C++ files, 15 conditionals; platform surface is `UIScheduler`, an animation-frame queue, `PlatformLogger` | `Common/cpp/worklets`, `apple/IOSUIScheduler.mm` | reuses `apple/` | JS fallback only | no | **owned by #95** |
| `react-native-share` 12.3.1 | 2 | `codegenConfig.type: "modules"` but the implementation is ObjC + Java; **a C++/WinRT Windows port exists** | `react-native-share/windows/ReactNativeShare` | no | **yes** | no | **port S** — the Windows port proves the logic is thin |
| `expo-splash-screen` 57.0.8 | 2 | Expo module; 2 Kotlin, 4 Swift | `expo-splash-screen/ios` | no | no | yes | **port S** — a splash on Wayland is our own first-frame policy |
| `expo-localization` 57.0.1 | 2 | Expo module; 3 Kotlin, 1 Swift | `expo-localization/android` | no | no | no | **port S** — POSIX locale + `xdg` settings portal; ties to #83 |
| `expo-linking` 57.0.8 | 2 | Expo module; 3 Kotlin, 3 Swift | `expo-linking/ios` | no | no | no | **port S** — `xdg-open` + a `.desktop` URL scheme handler |
| `expo-glass-effect` 57.0.0 | 2 | Expo module; **4 Swift files, 0 Kotlin** — already a no-op on Android | `expo-glass-effect/ios` | no | no | yes | **shim S** — mirror the Android no-op |
| `expo-constants` 57.0.16 | 2 (+ 1 spec) | Expo module; 2 Kotlin, 3 Swift | `expo-constants/ios` | podspec `osx 13.4` | no | no | **port S** — the manifest is already generated at build time |
| `react-native-svg` 15.15.4 | 1 (+ every Lucide icon) | 11 portable C++ files (`RNSVGLayoutableShadowNode`, component descriptors) + 212 Apple / 115 Java / **153 Windows** files | `common/cpp/react/renderer/components/rnsvg`, `windows/` | podspec `osx 10.14` | **yes** | **yes** | **port M/L** — a third renderer, but ours draws into Skia which already has the primitives |
| `react-native-screens` 4.26.0 | 1 | 25 portable C++ shadow-node/state files + 184 iOS + 181 Kotlin + a Windows project | `common/cpp/react/renderer/components/rnscreens`, `windows/RNScreens65.sln` | no | **yes** | yes | **port L**, or a JS `.linux` shim that degrades screens to Views first |
| `reanimated-color-picker` 5.1.2 | 1 | none — pure JS over Reanimated + RNGH | `package.json` | n/a | n/a | via those | **works once #95 and RNGH work** |
| `expo-sqlite` 57.0.2 | 1 (`kv-store`, the redux-persist backend) | Expo module; 11 Kotlin, 10 Swift, **plus JSI bindings in C++** under `android/src/main/cpp` (`NativeDatabaseBinding.cpp`); vendors sqlite3 and sqlcipher | `expo-sqlite/android/src/main/cpp`, `expo-sqlite/vendor/sqlite3` | podspec `osx 13.4` | no | no | **port M** — the C++ half is portable, sqlite3 is a system library; the Expo host is the cost |
| `expo-blur` 57.0.2 | 1 | Expo module; 7 Kotlin, 4 Swift | `expo-blur/ios/ExpoBlur.podspec` | no | no | **yes** | **port S** — `SkImageFilters::Blur` on a backdrop |
| `expo-screen-capture` 57.0.2 | 1 | Expo module; 2 Kotlin, 2 Swift | `expo-screen-capture/android` | no | no | no | **shim S** — Wayland has no client-side screenshot prevention; report unsupported truthfully |
| `expo-font` 57.0.2 | 1 | Expo module; 3 Kotlin, 7 Swift | `expo-font/ios` | podspec `osx 13.4` | no | yes | **port S** — folds into #70 (font asset registration) |
| `expo-status-bar` 57.0.1 | 1 | Expo module; 2 Kotlin, 0 Swift | `expo-status-bar/android` | no | no | no | **shim S** — no status bar on a desktop window |
| `@react-native-masked-view/masked-view` 0.3.2 | 0 (via `screen-chrome`) | ObjC + Java only; no shared C++, no `windows/`, no `macos/` | `@react-native-masked-view/masked-view` top level | no | no | **yes** | **port S** — a Skia layer mask; cheap for us, expensive everywhere else |
| `expo-linear-gradient` 57.0.1 | 0 (via `screen-chrome`) | Expo module; 2 Kotlin, 3 Swift | `expo-linear-gradient/android` | no | no | **yes** | **port S** — #34 already ships gradient fills |
| `react-native-nitro-modules` 0.36.1 | 0 (peer of unistyles) | Portable C++ core (69 files, **2** conditionals) + a declared platform contract | `cpp/platform/ThreadUtils.hpp`, `ios/threading/UIThreadDispatcher.cpp` | no | no | no | **port S — and it is the enabler** |
| `expo-system-ui`, `expo-updates`, `expo-dev-client`, `expo-build-properties`, `expo-asset`, `expo-file-system`, `expo-keep-awake` | 0 | Expo modules, pulled in by `expo` / `expo-router` / the dev loop | `expo-*/expo-module.config.json` | mixed | no | mixed | **port S each**, except `expo-updates` (92 Kotlin + 105 Swift — **decline**, we ship through packaging, #25) |

Pure-JS dependencies that need nothing: `@lingui/*`, `@reduxjs/toolkit`, `react-redux`, `redux-persist`,
`zod`, `date-fns`, `@rnw-community/shared`, `@formatjs/intl-pluralrules`, `@expo-google-fonts/inter`,
`react-native-web` (web only), `@expo/config-plugins`, `@expo/fingerprint`, `eas-build-cache-provider`
(build-time only) [V].

### The shape of the flagship's problem

- **30 direct dependencies ship native code. 19 of them are Expo modules**, 20 counting `expo-modules-core`,
  which they all sit on. [V]
- **Of the eleven non-Expo ones, three have no shared C++ at all**: `react-native-gesture-handler` (used in 5
  flagship files with `Gesture.Tap`, `Gesture.Pan` and `Gesture.Race`), `@react-native-masked-view/masked-view`
  and `react-native-share`.
- **3 ship portable Fabric C++** whose shadow nodes we can autolink today: `safe-area-context`, `screens`,
  `svg`.
- **2 are Nitro** (`nitro-modules`, `unistyles`) and both become tractable the moment Nitro core has a Linux
  platform layer.
- **1 is blocked on an architectural decision we already recorded**: `@shopify/react-native-skia` would put a
  second, differently-built Skia in the process (ADR-0001, *Additional accepted risks*).

---

## 2. The fifty most-downloaded libraries with native code

Ranked by npm downloads for the window `2026-07-31` → `2026-08-29`, restricted to packages that ship native
sources. Pure-JS packages with large counts are excluded from the ranking and listed after the table, because
"works on Linux" is not a question for them — with one instructive exception at rank 45.

Columns: **Surface** — the module system. **mac** / **win** — does a macOS or Windows implementation exist
(a proxy for "the author has already been forced to think about a third platform", not a guarantee of
anything). **Rnd** — does it touch the renderer. **Linux path** — `layer` = works through the compatibility
layer with zero Linux-specific code; `port S|M|L`; `upstream` = needs a change in the library; `decline` =
we should say no and record the substitute.

| # | Package | DL/mo | Surface | mac | win | Rnd | Linux path |
| ---: | --- | ---: | --- | :-: | :-: | :-: | --- |
| 1 | `react-native-safe-area-context` | 35.6M | Fabric C++ shadow node + ObjC/Kotlin view | ○ | – | ● | port S |
| 2 | `expo-constants` | 35.3M | Expo module | ○ | – | – | port S |
| 3 | `expo-file-system` | 34.7M | Expo module (28 kt / 32 swift) | – | – | – | port M |
| 4 | `expo-modules-core` | 34.1M | **Expo host**: 238 kt / 181 swift / 29 shared C++ | ○ | – | ● | port L — the gate for every row marked "Expo module" |
| 5 | `expo-font` | 33.9M | Expo module | ○ | – | ● | port S |
| 6 | `expo-asset` | 32.3M | Expo module | – | – | ● | port S |
| 7 | `expo-keep-awake` | 32.3M | Expo module | – | – | – | port S (`zwp_idle_inhibit_manager_v1`) |
| 8 | `react-native-screens` | 31.2M | Fabric C++ shadow nodes + 184 iOS / 181 kt | – | ● | ● | port L, or JS shim first |
| 9 | `react-native-reanimated` | 30.6M | 235 portable C++ + Apple/Kotlin glue | ○ | JS fallback | ● | **#95** |
| 10 | `react-native-gesture-handler` | 29.9M | **no shared C++**; two full implementations | ○ | – | ● | port L |
| 11 | `react-native-worklets` | 27.4M | 76 portable C++, 15 conditionals | ○ | JS fallback | – | **#95** |
| 12 | `@react-native-async-storage/async-storage` | 26.9M | Java/ObjC + a **C++ Windows impl** (`DBStorage.cpp`) | ● | ● | – | port S — overlaps #23 |
| 13 | `react-native-svg` | 26.3M | Fabric C++ shadow nodes + 3 full renderers | ● | ● | ● | port M/L |
| 14 | `expo-linking` | 26.3M | Expo module | – | – | – | port S |
| 15 | `expo-router` | 21.9M | Expo module, mostly JS | – | – | – | port S (after 8, 14) |
| 16 | `expo-splash-screen` | 21.5M | Expo module | – | – | ● | port S |
| 17 | `expo-application` | 21.2M | Expo module | – | – | – | port S |
| 18 | `expo-secure-store` | 21.0M | Expo module | – | – | – | port M (`libsecret` / Secret Service) |
| 19 | `expo-web-browser` | 19.4M | Expo module | – | – | – | port S (`xdg-open`) |
| 20 | `react-native-webview` | 18.0M | ObjC/Java + C++/WinRT | ● | ● | ● | port L (WebKitGTK in a subsurface) |
| 21 | `expo-image` | 17.3M | Expo module | – | – | ● | port M — overlaps #15 |
| 22 | `expo-notifications` | 16.9M | Expo module | – | – | – | port M (`org.freedesktop.Notifications`) |
| 23 | `expo-haptics` | 16.5M | Expo module | – | – | – | port S (no-op) |
| 24 | `expo-image-picker` | 15.9M | Expo module | – | – | – | port S (portal `FileChooser`) |
| 25 | `expo-dev-client` | 15.0M | Expo module + a native dev launcher UI | – | – | ● | decline — our dev loop is #79/#80/#81 |
| 26 | `expo-system-ui` | 14.9M | Expo module | – | – | ● | port S |
| 27 | `expo-crypto` | 13.7M | Expo module | – | – | – | port S (OpenSSL) |
| 28 | `expo-linear-gradient` | 13.5M | Expo module | – | – | ● | port S — #34 already draws it |
| 29 | `expo-updates` | 13.5M | Expo module, 92 kt / 105 swift | – | – | – | decline — packaging is #25 |
| 30 | `expo-device` | 13.3M | Expo module | – | – | – | port S |
| 31 | `@react-native-masked-view/masked-view` | 12.7M | ObjC + Java only | – | – | ● | port S |
| 32 | `@react-native-community/netinfo` | 11.5M | Java/ObjC + C++/WinRT | ● | ● | – | port S (NetworkManager over D-Bus) |
| 33 | `expo-clipboard` | 11.4M | Expo module | – | – | – | port S — overlaps #60 |
| 34 | `@sentry/react-native` | 11.0M | Java/ObjC wrappers; 1 shared C++ file | ● | – | – | port M — `sentry-native` is a real Linux SDK |
| 35 | `expo-blur` | 10.7M | Expo module | – | – | ● | port S |
| 36 | `@expo/ui` | 10.4M | Expo module over SwiftUI / Compose | – | – | ● | decline |
| 37 | `@react-native-community/datetimepicker` | 9.4M | Fabric C++ state + platform pickers + C++/WinRT | ○ | ● | ● | port M |
| 38 | `expo-location` | 9.3M | Expo module | – | – | – | port M (GeoClue) or decline |
| 39 | `expo-localization` | 9.1M | Expo module | – | – | – | port S |
| 40 | `react-native-keyboard-controller` | 7.9M | Fabric C++ shadow nodes (8) + kt/ObjC | – | – | ● | shim S — no soft keyboard on a desktop |
| 41 | `expo-sharing` | 7.9M | Expo module | – | – | – | port S |
| 42 | `expo-camera` | 7.9M | Expo module | – | – | ● | decline for now (pipewire/libcamera) |
| 43 | `expo-audio` | 7.6M | Expo module | – | – | – | port M (PipeWire) |
| 44 | `react-native-nitro-modules` | 7.5M | **portable C++ core + declared platform contract** | – | – | – | **port S — the enabler** |
| 45 | `@shopify/flash-list` | 7.4M | **none — 100% TypeScript in 2.x** | n/a | n/a | – | **layer — works today** |
| 46 | `expo-video` | 6.9M | Expo module | – | – | ● | port L or decline |
| 47 | `react-native-mmkv` | 6.2M | **Nitro, C++ hybrid** + a kt/swift platform context | – | – | – | **layer + a 1-method platform context** |
| 48 | `lottie-react-native` | 5.8M | Nitro (kt/swift impls) + C++/WinRT | ○ | ● | ● | port M — Skia ships `skottie` |
| 49 | `@react-native-firebase/app` | 5.7M | Java/ObjC over the Firebase SDKs | ● | – | – | decline / upstream |
| 50 | `@react-native-community/slider` | 5.5M | Fabric C++ shadow node + platform views + C++/WinRT | – | ● | ● | port S |

Just outside the fifty, and worth naming because they change the shape of an app rather than adding a
feature: `@shopify/react-native-skia` (5.3M, port L + `needs:decision`), `react-native-device-info` (5.3M,
port S), `react-native-pager-view` (5.0M, port M), `react-native-maps` (4.6M, decline),
`expo-sqlite` (4.1M, port M), `react-native-view-shot` (4.1M, port S — a Skia surface snapshot, which #33
already does), `@react-native-picker/picker` (3.9M, port M), `react-native-unistyles` (1.1M downloads but the
flagship's most-imported dependency), `@op-engineering/op-sqlite` (0.6M, **near-layer**: 24 C++ files in
`cpp/` around sqlite3, with thin platform adapters).

**Pure-JS packages with big numbers that gate on the rows above, not on us:**
`@react-navigation/native` (26.9M), `@gorhom/bottom-sheet` (11.7M), `react-native-toast-message` (2.4M),
`react-native-uuid` (2.2M), `react-native-collapsible` (0.8M), `react-native-modal-datetime-picker` (3.1M),
`lucide-react-native`, `reanimated-color-picker`. Each of them works the moment its native peer does.

### What the fifty actually say

| Bucket | Count | Note |
| --- | ---: | --- |
| Works via the layer with **zero** Linux-specific code, today | **1** | `@shopify/flash-list`, and only because it deleted its native code |
| Works via the layer after Nitro's three Linux platform files, with **no change to the library** | **1** | `react-native-mmkv`'s `MMKVFactory`; its `MMKVPlatformContext` still needs one Linux method |
| Expo modules (blocked behind one decision) | **26** | more than half the list |
| Ships portable Fabric C++ shadow nodes; port is the view half only | **6** | safe-area-context, screens, svg, keyboard-controller, datetimepicker, slider |
| Owned by the animation programme (#95) | **2** | reanimated, worklets |
| No shared C++ at all; a third implementation is required | **~14** | RNGH, masked-view, webview, netinfo, async-storage, device-info, … |
| Should be declined with a recorded substitute | **6** | `@expo/ui`, `expo-dev-client`, `expo-updates`, `expo-camera`, `react-native-maps`, `@react-native-firebase/*` |

The honest summary: **the C++-first layer buys us one library out of fifty today.** It is still the right
strategy, for three reasons the table also shows. It is the only thing that makes *future* libraries free —
Nitro and `cpp-library` are both growing, and both are portable by construction. It shrinks most ports from
"reimplement the module" to "implement a generated spec with a countable number of methods". And it is the
prerequisite for every port anyway: without autolinking, a CMake consumer and a registration path, even a
hand-written Linux port of a library has no way to reach the runtime.

---

## 3. The five native-module systems, and what each needs from us

| System | Author writes | Portable? | What Linux must provide |
| --- | --- | --- | --- |
| **C++ TurboModule** (`codegenConfig.type: "modules"` + a `cpp/` impl; `create-react-native-library`'s `cpp-library`) | one C++ class deriving the generated `<Name>CxxSpec` | **yes, fully** | three CMake target names and a generated `registerCxxModuleToGlobalModuleMap` call |
| **Fabric C++ component** (`ConcreteComponentDescriptor` + a C++ `ShadowNode`) | shadow node, props, state, descriptor | **the C++ half, yes** | descriptor registration into `FabricHost`, plus a mounted representation in `RetainedScene` |
| **Nitro module** (`nitro.json`, `nitrogen`) | a `HybridObject`; `"language": "c++"` is portable, `"swift"`/`"kotlin"` is not | **when the impl is C++** | Nitro's own platform files, plus a Linux `+autolinking.cmake` from `nitrogen` |
| **JSI library** (raw `jsi::HostObject`, no codegen) | a C++ host object installed at startup | **yes** | an install hook and a CMake target; `op-sqlite` is the archetype |
| **Expo module** (`expo-module.config.json`, the Kotlin/Swift `Module` DSL) | a Kotlin class and a Swift class | **no — there is no C++ author surface** | a third DSL host in C++, then a rewrite of every module body |

### 3.1 The C++ TurboModule path is already one file from working

`create-react-native-library` 0.63's `cpp-library` template, read at
`callstack/react-native-builder-bob@HEAD` [V]:

- `cpp/<Name>Impl.cpp` / `.h` — all of the logic, no platform includes.
- `android/CMakeLists.txt` — builds `../cpp/<Name>Impl.cpp` at C++20 and links **`jsi`**, **`reactnative`**,
  **`react_codegen_<Name>Spec`**.
- `ios/OnLoad.mm` — an ObjC `+load` that calls
  `registerCxxModuleToGlobalModuleMap(<Name>Impl::kModuleName, factory)`.
- `react-native.config.js` — `dependency.platforms.android` with `cmakeListsPath`,
  `cxxModuleCMakeListsModuleName`, `cxxModuleCMakeListsPath`, `cxxModuleHeaderName`.

Two consequences. First, `packages/core` must export CMake targets literally named `jsi`, `reactnative` and
`react_codegen_<Name>Spec`; it currently has `jsi` and `react_codegen_rncore`, and its umbrella is
`rnl_react_core` [V]. Aliases close that gap and the template's own CMakeLists then compiles unchanged.
Second, **registration is the only genuinely platform-specific artifact in the template**, and it is four
lines. We generate it rather than asking authors for a third copy.

`registerCxxModuleToGlobalModuleMap` is declared in
`ReactCommon/react/nativemodule/core/ReactCommon/CxxTurboModuleUtils.h` and is entirely platform-neutral;
its docblock even says *"for example in a `+ load`, your AppDelegate's start, or from Java init"* [V]. There
is nothing to fork.

### 3.2 Fabric C++ components split cleanly in two

`FabricHost::registerComponentDescriptors` in `packages/core/src/FabricHost.cpp` calls
`concreteComponentDescriptorProvider<T>()` for each built-in component [V]. A third-party descriptor joins
that list mechanically. What does not join mechanically is the mounted view: `RetainedScene`'s `SceneNode`
is a closed record with optional `text`, `image` and `editor` payloads [V], and an `RNSVGCircle` has no
representation in it.

That gives two tiers, and only the first is free:

- **Tier A — layout-only components.** The component contributes layout and `ViewProps` and nothing else:
  `RNCSafeAreaView`, `RNSScreenContainer`, most of `keyboard-controller`'s views, any wrapper component. It
  mounts as a `View` and is correct. This is a real slice of the ecosystem and it costs one code path.
- **Tier B — custom paint.** SVG shapes, masked views, blurs, Lottie. Each needs a scene-node kind and a
  paint routine.

Per the Prime Directive we do **not** design a component plugin system for Tier B before the second consumer
exists. `ecosystem.json` records this as a `needs:decision` issue gated on `react-native-svg` and
`masked-view` both landing, not as scaffolding to build first.

### 3.3 Nitro is the best-shaped integration in the survey

Evidence, all read locally at 0.36.1 [V]:

- `cpp/` — 69 files, **2** platform conditionals (`utils/NitroDefines.hpp:21`, `utils/NitroTypeInfo.cpp:51`),
  zero fbjni includes, zero Objective-C includes.
- `cpp/platform/ThreadUtils.hpp` — `getThreadName`, `setThreadName`, `isUIThread`,
  `createUIThreadDispatcher`. `cpp/platform/NitroLogger.hpp` — one `nativeLog`.
- The iOS implementation of all of it is three files: `ios/platform/NitroLogger.cpp`,
  `ios/platform/ThreadUtils.cpp`, `ios/threading/UIThreadDispatcher.cpp` (19 lines of header over a
  `dispatch_queue_t`). Android's is the same three, over a JNI looper.
- `nitrogen` emits `nitrogen/generated/android/<name>+autolinking.cmake` and
  `nitrogen/generated/ios/<Name>+autolinking.rb`. The CMake file is real and complete — it lists the shared
  C++ sources, the Android-specific ones, and links `fbjni`, `ReactAndroid::jsi`,
  `react-native-nitro-modules::NitroModules` [V]. **The shape we need already exists; only its
  Android-specific half is wrong for us.**

And `nitro.json` is a machine-readable port ledger. From `react-native-mmkv` [V]:

```json
"autolinking": {
  "MMKVFactory":         { "all":     { "language": "c++",    "implementationClassName": "HybridMMKVFactory" } },
  "MMKVPlatformContext": { "ios":     { "language": "swift",  "implementationClassName": "HybridMMKVPlatformContext" },
                           "android": { "language": "kotlin", "implementationClassName": "HybridMMKVPlatformContext" } }
}
```

`"all"` means portable. Every non-`"all"` key is a Linux implementation we owe, and the generated
`Hybrid*Spec.hpp` in `nitrogen/generated/shared/c++` states its exact size: `react-native-unistyles`'
`HybridNativePlatformSpec` has **18** pure-virtual methods — insets, colour scheme, font scale, pixel ratio,
orientation, dimensions, RTL, four setters and three listener hooks [V]. That is a port we can scope from a
header instead of from a guess.

### 3.4 Expo modules are the wall

Everything in this subsection is verified.

- `expo-module.config.json` declares `"platforms": ["apple", "android"]` plus per-platform `modules: [<class
  name>]`. Blur: `{"apple": {"modules": ["BlurViewModule"]}, "android": {"modules":
  ["expo.modules.blur.BlurModule"]}}`.
- `expo-modules-autolinking`'s `SupportedPlatform` type is
  `'apple' | 'ios' | 'android' | 'web' | 'macos' | 'tvos' | 'devtools' | (string & {})`, but
  `getLinkingImplementationForPlatform` has exactly four `require`s and a `default:` that throws
  `No linking implementation is available for platform "<x>"`.
- `getSupportPackageForPlatform` in the same file **already has** `case 'windows': return
  'react-native-windows'`, with no corresponding linking implementation. A `case 'linux': return
  '@react-native-linux/core'` is a one-line, precedented upstream PR — and it buys discovery only.
- `expo-modules-core` ships 238 Kotlin files and 181 Swift files. Its `common/cpp` is 29 files:
  `SharedObject`, `SharedRef`, `EventEmitter`, `NativeModule`, `LazyObject`, `JSI/TypedArray`,
  `JSI/JSIUtils`, and `fabric/ExpoView{Props,State,ShadowNode,EventEmitter,ComponentDescriptor}`. That is a
  **JSI object model**, not a module DSL. The DSL — `Module { Name(...); Function(...); View(...) }` — exists
  only in Kotlin and Swift.
- Not one Expo module in the flagship ships a `common/cpp` directory. `expo-sqlite` and `expo-updates` have
  C++ under `android/src/main/cpp` (fbjni-bound JSI bindings and a bsdiff module) and nowhere else.

So there are exactly three options, and `ecosystem.json` files them as one `needs:decision` issue rather
than presupposing an answer:

- **(a) A C++ Expo host.** Reuse `common/cpp`'s object model, write a C++ `ModuleDefinition` DSL, then
  reimplement each module's Kotlin body in C++. Highest fidelity, highest cost, and it forks a moving target.
- **(b) A JS shim tier.** `.linux.ts` implementations of each module's JS surface over a handful of our own
  C++ TurboModules. `lucid-softworks/react-native-linux` already ships **26** such shims (ADR-0001, *Live
  projects on desktop Linux today*), which is the only in-the-wild evidence either way. Cheap, honest about
  its own limits, and it covers the flagship's whole thin tier: constants, font, haptics, linking,
  localization, splash-screen, status-bar, system-ui, sharing, screen-capture, glass-effect.
- **(c) Neither.** Tell app authors to drop Expo. This is what `react-native-windows` effectively did, and
  ten years later "Expo support" (rn-windows#13534) is still its most-reacted open issue. Recorded so the
  cost of choosing it is visible.

The recommendation this report makes is **(b) first, (a) only if the flagship proves it necessary** —
because (b) reaches eleven flagship dependencies with no new module system, and because the modules it
cannot reach (`expo-sqlite`, `expo-image`, `expo-notifications`) are exactly the ones that want a C++
TurboModule of our own anyway.

---

## 4. The compatibility layer

Five parts. Nothing in it is speculative infrastructure: every piece exists because a library read for this
report needs it.

### 4.1 Discovery

The community CLI's `config` command is the single source of truth on both shipping platforms, and our
`platforms.linux` registration already lands in the same JSON [V, `docs/research/codegen-and-oot-platform-tooling.md`
§4.5]. `packages/cli/src/platform-config.ts` already implements `dependencyConfig` [V]. The discovery rules,
in order:

1. **Explicit** — `dependency.platforms.linux` in the library's `react-native.config.js`, carrying the same
   keys the Android descriptor uses: `cmakeListsPath`, `cxxModuleCMakeListsPath`,
   `cxxModuleCMakeListsModuleName`, `cxxModuleHeaderName`, and a `sourceDir` for a `linux/` directory.
   Deliberately the same names — an author adding Linux support copies six lines and changes nothing else.
2. **Pure-C++ fallback** — if `platforms.linux` is absent but `platforms.android` declares the `cxxModule*`
   keys, treat the library as Linux-compatible and use the Android descriptor's `cxxModuleCMakeListsPath`.
   This is what makes a `cpp-library` work with **zero** author involvement, and it is the rule that gives
   the strategy its name. It is a guess, so it is guarded: the build fails loudly if the referenced sources
   include `<jni.h>`, `<fbjni/`, or an Objective-C header.
3. **Opt-out** — `dependencies: { foo: { platforms: { linux: null } } }` already works upstream, unchanged
   [V, `generate-artifacts-executor/utils.js:466`].
4. **Nitro** — a `nitro.json` at the package root. Every `autolinking` entry with an `"all"` key is
   compiled; every entry with only `ios`/`android` keys is reported as an unimplemented hybrid object with
   its spec header path, so the gap is a diagnostic rather than a link error.
5. **Expo** — an `expo-module.config.json` whose `platforms` array lacks `linux` is reported as "not
   supported on Linux, see the Expo decision", never silently skipped.

### 4.2 Build integration

One generated file, `build/generated/autolinking/rnl_autolinking.cmake`, `include()`d by the app's
CMakeLists, containing per discovered library either an `add_subdirectory()` of its own CMakeLists or an
`add_library()` over its declared C++ sources, plus `target_link_libraries` against the core.

For that to work `packages/core` exports three **alias** targets whose names are fixed by what libraries
already write: `jsi` (exists), `reactnative` (alias of `rnl_react_core`'s public interface), and
`react_codegen_<Name>Spec` per autolinked library, produced by the `codegen-linux` driver from #21. Nothing
else is invented; the names come from the upstream template, not from us.

Cache-keying follows the Gradle plugin's precedent: a SHA-256 over the lockfile, `package.json` and every
`react-native.config.js` [V].

### 4.3 Registration

The one artifact no platform can inherit. Android uses `JNI_OnLoad`, Apple uses ObjC `+load`; we have
neither. The layer generates a single `RegisterAutolinkedModules.cpp` containing, per discovered library:

- a `registerCxxModuleToGlobalModuleMap(name, factory)` call for each C++ TurboModule, and
- a `providerRegistry->add(concreteComponentDescriptorProvider<T>())` call for each Fabric C++ component,
  invoked from `FabricHost::registerComponentDescriptors`, and
- a `HybridObjectRegistry::registerHybridObjectConstructor(...)` call for each `"all"` Nitro hybrid object,
  which is what `nitrogen`'s generated `<name>OnLoad.cpp` does on the other platforms [V].

Generated, compiled into the app target, and covered by the 100% C++ gate like everything else.

### 4.4 The Linux support contract

A page in `docs/` that a library author can satisfy without owning a Linux machine. Four rules, each of
which is already true of at least one library in the survey:

1. **Put the logic in `cpp/`.** If a module's behaviour is expressible in C++, `cpp-library` or a C++ Nitro
   hybrid object makes it portable by construction. `react-native-mmkv` is the proof.
2. **Do not put platform code behind an unguarded `#ifdef`.** Reanimated's
   `PlatformDepMethodsHolder.h` defines a type under `#ifdef ANDROID` / `#elif __APPLE__` with no `#else`,
   and a struct member of that type — so the header does not *compile* on a third platform [V, and see
   `docs/research/animation-on-linux.md` §4.2]. An `#else` with a documented default costs nothing and is the
   difference between "needs a port" and "needs a fork".
3. **Declare the platform seam.** Nitro's `nitro.json` and its generated `Hybrid*Spec.hpp` are the model:
   the set of things a new platform owes is enumerated, typed, and countable. A library that states its seam
   converts an unbounded port into an 18-method one.
4. **Keep the C++ half of a Fabric component in `common/cpp`.** Six of the top fifty already do; those six
   get their shadow nodes, props, state and descriptors on Linux for free.

The contract also states what we guarantee back: the three CMake target names, the generated registration,
the codegen output shape from #21, and a stated ABI boundary (#89).

### 4.5 The conformance kit

`react-native-windows`' own conclusion (rn-windows#15078, *"Streamline RNW Community Modules Integration &
Creation Process"*) is that a tracker full of "does X work on your platform?" issues is the cost of not
shipping this. The kit is a command a library author runs in their own CI:

- **Static** — resolve the package the way our autolinking does and report which of the five discovery rules
  matched, or why none did.
- **Compile** — build the library's C++ against the core headers, with `-Werror` on the conditional-compile
  traps: an unguarded platform `#ifdef`, a `<jni.h>` or Objective-C include reached from a "portable" source.
- **Runtime** — load the library into the headless harness under the lavapipe/headless-Wayland rig (#7, #33),
  call every generated spec method with the conformance fixtures from #85's type surface, and assert the
  event trace.
- **Report** — a machine-readable verdict the ecosystem matrix in #87 consumes, so the matrix is generated
  rather than curated.

The kit deliberately reuses #85 (codegen determinism and spec coverage) and the existing harness rather than
growing a second test system.

---

## 5. Where this fits the work already planned

| Existing issue | Relationship |
| --- | --- |
| #21 codegen + autolinking | The layer's discovery and CMake consumer are the autolinking half of #21, expanded into their own issues here. `codegen-linux` produces `react_codegen_<Name>Spec`. |
| #22 platform registration, #55 resolver chain | Already landed the `.linux → .native → default` chain the JS shim tier depends on. |
| #56 autolinking and codegen error surface | The "report why a library did not autolink" issue here is its ecosystem-facing half. |
| #87 ecosystem support matrix | Owns the *ledger*. This report owns the *survey and the layer*; the conformance kit feeds #87's matrix so it is generated, not curated. |
| #88 library template and authoring path | Owns the template. The support contract here is its documentation half; they must not duplicate the CI job. |
| #89 prebuilt core binaries and ABI boundary | The support contract cannot promise anything the ABI boundary has not stated. |
| #95 animation programme | Owns Reanimated and worklets outright. |
| #24 flagship bring-up, #25 packaging | Consume everything here. |
| #23 core native modules | Already plans Appearance, Dimensions, Clipboard, Linking and an AsyncStorage-compatible store — which pre-empts four of the top-fifty rows. |

---

## 6. Rejected alternatives, with reasons

### 6.1 Emulate the Objective-C runtime so `react-native-macos` binaries load

**Rejected.** The technical premise is not the problem; GNUstep and `libobjc2` exist and Objective-C compiles
on Linux with clang. The problem is that no library in the survey ships an Objective-C *binary* — they ship
sources that `#import <UIKit/UIKit.h>`, `<AppKit/AppKit.h>`, `<Foundation/Foundation.h>`, `<Metal/Metal.h>`,
`<AVFoundation/AVFoundation.h>` and `<CoreGraphics/CoreGraphics.h>`. `react-native-svg`'s `apple/` directory
alone is 212 files against UIKit and CoreGraphics, and ships compiled Metal libraries
(`apple/**/*.macosx.metallib`) [V]. Emulating the ABI would deliver the ability to load code that then fails
at its first framework symbol. The frameworks, not the runtime, are the dependency — and reimplementing
UIKit is strictly harder than reimplementing the twenty libraries that use it.

Second reason: it would make our paint model UIKit's paint model, which reverses ADR-0001's central decision
(*"On a canvas the mismatch cannot occur, because there is no second model"*).

### 6.2 Emulate the Android JNI ABI so `.so`s and Kotlin modules load

**Rejected**, and more decisively. A JNI shim is the easy half; ART, the Android framework classes
(`android.content.Context`, `android.view.View`, `androidx.*`), Google Play services and the Android resource
system are the library. `react-native-gesture-handler`'s Android half is 62 Kotlin files whose types are
`MotionEvent`, `ViewGroup` and `ViewConfiguration` [V] — running them would mean running Android. Amazon Vega
is the proof by example: it is a Linux-based OS that runs React Native, and it did **not** do this; it ported
React Common, Yoga and Hermes and wrote its own native layer (ADR-0001, *Prior art*).

### 6.3 Ship a `react-native-web` bridge for native libraries

**Rejected as a platform strategy**, consistent with ADR-0001's rejection of the webview path. It would make
`Platform.OS` a lie for any library that branches on it, and the `.web.js` implementation of a native library
is usually a stub (`react-native-share`'s web path is `navigator.share`; `expo-haptics`' is nothing). Where a
library's own `.web` implementation is genuinely complete it remains a legitimate *shim source* for the JS
tier in §3.4(b) — used deliberately, per library, and recorded, not adopted as a policy.

### 6.4 Wait for upstream to define an out-of-tree module contract

**Rejected as a plan**, retained as an activity. `react-native-community/discussions-and-proposals#195`
asked "what steps are involved in making an out-of-tree platform work with TurboModules?" in 2020 and the
answer in the thread is still literally `?` [V, `docs/research/codegen-and-oot-platform-tooling.md` §1.3].
We do file the one-line upstream PRs that cost nothing and compound —
`expo-modules-autolinking`'s `getSupportPackageForPlatform`, a `linux` target in `nitrogen`, a Linux section
in `create-react-native-library`'s `cpp-library` — but nothing on the roadmap waits on them.

### 6.5 Fork every library into a `@react-native-linux/*` scope

**Rejected.** It converts a one-time port into a permanent merge obligation against a moving upstream, for
twenty libraries, with the release cadence of each. ADR-0001 already accepts one such obligation (the
`react-native` JS overrides, tracked through `react-native-platform-override`) and calls it out as a
recurring cost. Twenty more is not a plan. Ports are contributed upstream where the maintainer will take
them, and carried as `react-native-platform-override`-style patch sets where they will not.

---

## 7. The first ten ports

Ranked by flagship need × downloads × inverse port size. The order is an execution order: cheap unblockers
first, then the two large renderer ports, then the gesture machine.

| # | Port | Size | DL/mo | Flagship | Why here |
| ---: | --- | :-: | ---: | :-: | --- |
| 1 | **`react-native-nitro-modules`** — `ThreadUtils`, `NitroLogger`, `UIThreadDispatcher` | S | 7.5M | yes (peer of unistyles) | Three files unlock every C++ Nitro hybrid object, `react-native-mmkv` as the proof, and `unistyles` as the consumer. Highest leverage per line in the survey. |
| 2 | **`react-native-safe-area-context`** | S | 35.6M | yes | The most-downloaded native library in the ecosystem, its shadow node is already portable C++, and a window's insets are something the compositor tells us. |
| 3 | **The Expo thin-shim tier** — constants, font, haptics, linking, localization, splash-screen, status-bar, system-ui, sharing, screen-capture, glass-effect | M (all eleven) | 33M median | yes | Eleven flagship dependencies for one piece of work, no new module system, and it makes the Expo host decision an informed one instead of a bet. |
| 4 | **`react-native-unistyles`** — the `NativePlatform` hybrid object | S/M | 1.1M | **yes, 96 imports** | The flagship's most-coupled dependency by a factor of three. The spec header states the port: 18 methods. Depends on #1. |
| 5 | **`expo-sqlite` / `kv-store`** | M | 4.1M | yes (redux-persist backend) | Nothing the flagship does survives a restart without it. The C++ JSI bindings are portable and sqlite3 is a system library; only the Expo host wrapping them is not. |
| 6 | **`@react-native-masked-view/masked-view`** | S | 12.7M | yes (via `screen-chrome`) | A layer mask is one Skia call for us and a `CALayer`/`ViewOutlineProvider` fight everywhere else. The cheapest demonstration that a canvas platform makes some ports *easier*. |
| 7 | **`expo-blur` + `expo-linear-gradient`** | S | 24.2M combined | yes (via `screen-chrome`) | Two renderer-touching Expo modules whose entire implementation is a draw we already have (#34 gradients, `SkImageFilters::Blur`). |
| 8 | **`react-native-screens`** | L (or S as a JS shim first) | 31.2M | yes | Gates `expo-router`, which gates the flagship's navigation. Land the JS shim that degrades screens to Views, prove the app navigates, then decide whether the native port is worth it. |
| 9 | **`react-native-svg`** | M/L | 26.3M | yes (every Lucide icon) | The flagship's entire icon set. A third renderer — but the third renderer is Skia paths, which is the one part of the job we are already built for. |
| 10 | **`react-native-gesture-handler`** | L | 29.9M | yes | The only flagship dependency with no shared C++ at all, so it is a full third implementation of the orchestrator and every recogniser. Last because it is the largest, first among the L-sized ports because `Gesture.Pan` is in the flagship's board interaction. |

Deliberately **not** in the ten, with reasons: `react-native-reanimated` and `react-native-worklets` (owned
by #95, and larger than anything here); `@shopify/react-native-skia` (blocked on the two-Skia-builds
decision, which is an ADR before it is a port); `@expo/ui`, `expo-updates` and `expo-dev-client` (proposed
declines); `react-native-mmkv` (not a port — a *verification*, and it is filed as one).

---

## 8. Open questions, and what this report leaves ambiguous

1. **The Expo host decision is not made here.** §3.4 recommends the JS shim tier first and records the three
   options with their costs, but choosing (a) later is a genuine fork in the roadmap and it is filed as
   `needs:decision`, not as a plan. **[?]**
2. **Tier-B Fabric component paint is deliberately undesigned.** The scene extension point is gated on
   `react-native-svg` and `masked-view` both landing, per the Prime Directive's "second concrete consumer"
   rule. Until then the layer supports Tier A only, and that is a stated limitation rather than an oversight.
3. **`react-native-screens`: shim or port is unresolved.** A JS shim that degrades screens to Views is cheap
   and may be enough for a desktop app that has no native navigation transitions to match. Whether
   `expo-router` tolerates it has not been tested. **[?]**
4. **The pure-C++ fallback discovery rule (§4.1 rule 2) is a heuristic.** It assumes that a library declaring
   `cxxModule*` keys under `platforms.android` is portable. The guard (fail on JNI/ObjC includes) is a
   proxy, not a proof, and the first false positive will teach us something the report cannot. **[I]**
5. **Download counts are a popularity proxy, not a usage proxy.** Expo modules are installed transitively by
   `expo` and `expo-router`, which inflates them relative to standalone libraries; `react-native-unistyles`
   at 1.1M outranks almost everything for *this* app. Both signals are in the table; the ranking in §7 uses
   flagship coupling as the tie-break for exactly this reason.
6. **Port sizes are read from file counts and platform-seam headers, not from having done them.** The S/M/L
   column is calibrated against `react-native-nitro-modules` (3 files = S) and
   `react-native-gesture-handler` (119 platform files with no shared C++ = L). Everything between those is
   an estimate. **[I]**
7. **Not checked: whether `nitrogen` will accept a third platform.** The generated
   `<name>+autolinking.cmake` is Android-shaped (fbjni, `ReactAndroid::` prefabs) and a Linux variant needs
   either an upstream `nitrogen` target or a generator of ours reading `nitro.json` and
   `nitrogen/generated/shared/c++`. The second is what §4.1 rule 4 assumes; the first is the upstream PR
   worth filing. **[?]**
8. **Not checked: `@shopify/react-native-skia` against our Skia.** Whether two Skia builds can coexist in one
   process, or whether the library can be made to link *our* Skia, was not investigated. ADR-0001 raises it;
   nothing here resolves it. **[?]**
9. **Not checked: whether the community CLI's `config` output survives the announced move.** RN core's own
   `react-native.config.js` calls its platform wiring *"a temporary workaround"* [V,
   `docs/research/codegen-and-oot-platform-tooling.md`], so the discovery seam §4.1 builds on is explicitly
   slated to change.

---

## Sources

Primary sources read on this machine or through `gh api` / the npm registry on 2026-09-03. Every claim marked
**[V]** above traces to one of these.

- `~/.t3/worktrees/suuudokuuu/t3code-c2e11773/packages/app/{package.json,app.config.js,src}` — the flagship
  at 2.12.1, its Expo plugin list, and its import graph.
- `~/suuudokuuu/node_modules/{react-native-*,@shopify/react-native-skia,@react-native-masked-view/masked-view,expo-*,@expo/*}`
  — the resolved native trees every classification is read from.
- `~/suuudokuuu/node_modules/react-native-nitro-modules/cpp/platform/{ThreadUtils.hpp,NitroLogger.hpp}`,
  `ios/threading/UIThreadDispatcher.hpp` — the Nitro platform contract.
- `~/suuudokuuu/node_modules/react-native-unistyles/nitrogen/generated/{shared/c++,android/unistyles+autolinking.cmake}`
  — the Nitrogen output shape and the 18-method spec.
- `~/suuudokuuu/node_modules/expo-modules-autolinking/src/platforms/index.ts`,
  `expo-modules-core/{expo-module.config.json,common/cpp}` — the Expo platform dispatch and object model.
- `third_party/react-native/packages/react-native/ReactCommon/react/nativemodule/core/ReactCommon/CxxTurboModuleUtils.h`,
  `ReactCxxPlatform/` — the platform-neutral registration seam.
- `node_modules/@react-native/codegen/lib/generators/RNCodegen.js` — `modulesCxx: [generateModuleH.generate]`.
- `packages/core/{CMakeLists.txt,src/FabricHost.cpp,src/RetainedScene.h}`,
  `packages/cli/src/{platform-config.ts,metro-config.ts}` — what has already landed here.
- `gh api repos/callstack/react-native-builder-bob/contents/packages/create-react-native-library/templates/cpp-library/**`
  — the template's CMakeLists, `OnLoad.mm` and `react-native.config.js`.
- `gh api repos/{Tencent/MMKV,mrousavy/react-native-mmkv,Shopify/flash-list,software-mansion/*,zoontek/*,…}/git/trees/HEAD?recursive=1`
  — file-layout evidence for every top-fifty row not installed locally.
- `https://api.npmjs.org/downloads/point/last-month/<pkg>` — every download figure, single window.
- `docs/adr/0001-gpu-first-out-of-tree-react-native-platform-for-linux.md` (M4 scope, accepted risks),
  `docs/research/prior-art.md`, `docs/research/codegen-and-oot-platform-tooling.md`,
  `docs/research/animation-on-linux.md` — the prior record this report builds on and does not restate.
