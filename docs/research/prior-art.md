# `react-native-linux` — Prior Art & Feasibility Report

**Date: 2026-09-01. Baseline: React Native 0.87.1 (released 2026-08-26).**

**Verification key:** **[V]** verified against primary source I read directly · **[S]** sourced from a delegated research stream, not independently re-verified by me · **[I]** inference · **[?]** unresolved.

**Two framing facts before anything else.** First, the repo moved: `facebook/react-native` now redirects to **`react/react-native`**, because as of **2026-02-24** React and React Native are owned by the **React Foundation** under the Linux Foundation, with eight platinum founding members — **Amazon, Callstack, Expo, Huawei, Meta, Microsoft, Software Mansion, Vercel** [V, [react.dev](https://react.dev/blog/2026/02/24/the-react-foundation)]. Five of those eight ship or build out-of-tree RN platforms. Second, and this is the headline: **React Native already contains a generic C++ host platform and already builds and runs the full Fabric + Hermes stack on x86-64 Linux in Meta's own CI on every pull request.** The project you are considering is much less greenfield than the official documentation implies.

---

## 1. Prior art — every known attempt, status as of 2026

### 1.1 The Skia RN renderer (this is *not* Microsoft's)

Your premise needs one correction. **`microsoft/react-native-skia` does not exist** — `gh api repos/microsoft/react-native-skia` returns 404, and I found no evidence it ever did [V]. The project the RN docs actually point to is **`react-native-skia/react-native-skia`** [V, raw doc source at [`out-of-tree-platforms.md`](https://github.com/react/react-native-website/blob/main/docs/out-of-tree-platforms.md)], listed under *Community* as *"React Native using Skia as a renderer. Currently supports Linux and macOS."*

| Fact | Value |
|---|---|
| Created / last commit / last push | 2020-09-04 / **2023-03-24** / 2023-06-12 [V] |
| Stars, license | 1,089, MIT [V] |
| RN version | **0.71** — sixteen minors behind 0.87 [V] |
| Self-assessment | *"still in proof of concept and requires lots of work"* [V, README] |
| Platform | *"Only Linux (Ubuntu 18) is supported in the mean time"* [V] |
| Build | Chromium `depot_tools` + `gclient` + `gn` [V] |
| Contributors | 12 total; top are `pprabhu-ng`, `munezbn`, `shefayaparwin` — **NAGRA/OpenTV** set-top-box engineers — plus `Kudo` (Kudo Chien, Expo) [V] |

The `nagra-opentv` GitHub org tells the real story: they first forked `react-native-dom` and `yoga-dom` (2021), abandoned that, then went Skia, then added `buildroot-react-native-skia` for embedded images (2023) [V]. **Roughly 8–10 engineers over ~2.5 years produced a self-declared proof of concept, then stopped.** Do not confuse any of this with Shopify's `@shopify/react-native-skia` (8.5k stars, active 2026-09-01), which is a *drawing library for* RN, not a renderer *of* RN [V].

### 1.2 react-native-windows and react-native-macos

Both are **full forks of the react-native repository**, not plugins. `react-native-macos` shows Kotlin, Java, and Objective-C++ in its language stats because it carries the entire Android and iOS tree along for the ride [V].

| | RNW | RN-macOS | RN core |
|---|---|---|---|
| Latest npm | **0.84.0** (2026-06-18) | **0.81.9** (2026-07-13) | **0.87.1** (2026-08-26) |
| Lag behind core | **3 minors** | **6 minors** | — |
| Weekly downloads | 73,162 | 102,425 | — |
| Stars / open issues | 17,335 / **798** | 4,384 / 104 | 126,472 |
| C++ in repo | 5.6 MB | 6.0 MB | — |
| License | MIT | MIT | MIT |
| Last push | 2026-09-01 | 2026-08-17 | 2026-09-01 |

[all V, via `gh api` and the npm registry]

Both are alive and Microsoft-maintained. The **structural lesson is the lag**: two platforms with a full-time vendor team behind them sit 3 and 6 minor versions behind core, permanently. `react-native-platform-override` — the npm tool Microsoft built specifically to manage patches applied to RN core files for an out-of-tree platform — exists precisely because forking core is unavoidable [S, **[?]** on its 2026 maintenance status; I could not close this].

**Sustainability counter-example worth weighing heavily:** `callstack/react-native-visionos` is a fork of RN core by a top-tier RN agency, started 2023-09-11. Last npm publish **0.79.6 on 2025-08-23**; last repo push 2025-09-03 [V]. **It has been stalled for a full year** — and visionOS shares almost its entire native stack with iOS. If the easiest possible out-of-tree platform stalls, that is a signal about the maintenance cost, not about Callstack.

### 1.3 The dead desktop generation

- **`status-im/react-native-desktop-qt`** — 1,248 stars, Qt Quick/QML, descended from **Canonical's** 2016 Ubuntu-convergence RN port. Supported Linux, macOS, Windows. Last substantive commit 2020-05-29; final commit **2021-02-16, message: "Project support stopped"**; README banner: *"⚠️ React Native Desktop project support stopped"* [V]. Architecture was pre-Fabric, pre-JSI: JSON messages to a bundler process, with `ReactView.qml` / `ReactText.qml` / `ReactScrollView.qml` paired 1:1 against C++ `componentmanagers/` [S]. Status.im subsequently rewrote its desktop client natively in Nim + QML [S]. No stated post-mortem [V].
- **Name-collision warning:** two abandoned forks of it are *literally named* `react-native-linux` — `DensityCo/react-native-linux` (archived) and `dmgctrl/react-native-linux` (14 stars, last push 2017-09-26) [V].
- **Proton Native** — 10,882 stars, MIT. npm `proton-native` last published **2020-01-19**; repo last push 2023-01-07. v1 was libui, v2 a Qt rewrite; author stepped away [V/S]. Dead.
- **`react-native-gtk`** — npm `0.0.1-prerelease.7`, **2017-10-12**, no license [V]. Dead.
- **`react-native-dom`** (Vincent Riemer) — real Yoga + shadow tree in the browser; NAGRA forked it in 2021 before abandoning that direction [V]. Dead.
- **`react-native-desktop`** (npm) — `0.9.0-rc1`, **2016-07-16**, BSD-3 [V]. Dead.

### 1.4 The vendor ports that prove portability — and these are the important ones

**Amazon Vega OS / Kepler — the single strongest precedent, because Vega is Linux.** Amazon shipped a **Linux-based** OS replacing Android-based Fire OS, announced 2025-09-30, whose entire app layer is React Native [V, [developer.amazon.com](https://developer.amazon.com/apps-and-games/blogs/2025/09/announcing-vega-os)]. From Amazon's own architecture docs [V, [vega-rn-arch](https://developer.amazon.com/docs/vega/0.23/vega-rn-arch.html)]:

> *"React Native for Vega is an out-of-tree fork of React Native framework for Vega devices… RNV ported the React Native software stack including **React Common, Yoga, and Hermes** for Vega OS to run on top of Vega's native UI framework."*

It uses **Fabric**, **Hermes**, **JSI-only** (no bridge), **TurboModules**, a **GPU-accelerated Vega compositor**, and supports **>80% of RN core components plus ~40 ecosystem libraries**. Uniquely, the RN runtime is **bundled into the system image and dynamically linked at app launch**, with process pre-warming — not statically linked per app. Developers can additionally render with **WebGPU, Skia, or Filament**. Software Mansion is a named partner [V, [swmansion blog](https://swmansion.com/blog/amazon-x-software-mansion-powering-vega-os-with-react-native-75c4cf522fd7/)]. Note a version discrepancy I could not resolve: Amazon's 0.22/0.23 docs say RN **0.72**, while the npm package `@amazon-devices/react-native-kepler` reportedly reads RN **0.83.0** [S] — **[?]**.

**Huawei OpenHarmony (RNOH) — the most recent full out-of-tree platform build-out.** Built by **Huawei with Software Mansion**, on the New Architecture [V, [swmansion blog](https://swmansion.com/blog/huawei-x-software-mansion-bringing-react-native-support-to-harmonyos-next-82e02bd75549/), published 2026-01-14]. Their own words:

> *"Although Fabric is mostly written in C++, integrating it with HarmonyOS Next still posed significant challenges. We had to put considerable effort into bridging the gap between ArkTS and React Native's codebase."*
> *"[We] built the native OpenHarmony implementation of the TurboModule system from scratch."*

Status: **supports RN 0.77 and 0.72, currently working on 0.82** — five minors behind core [V]. The canonical repo (`OpenHarmony-SIG/ohos_react_native`, mirrored to gitee/gitcode/atomgit) has **1,156 commits and 54 releases** [S]. The `react-native-harmony` GitHub org maintains a **fork of react-native core** (`parent: react/react-native`) [V]. And the `react-native-oh-library` org contains **290 repositories** — forks of `react-native-reanimated`, `react-native-screens`, `react-native-video`, `react-navigation`, `react-native-permissions`, and so on, still being updated in 2026 [V]. **That is the true cost of an out-of-tree platform: you fork the ecosystem, not just the core.**

**Other vendor ports:** react-native-tvos (`react-native-tvos/react-native-tvos`, 1,402 stars, active 2026-08-31, MIT, full RN fork) [V]. LG webOS, Samsung Tizen, Kodi — **[?]**, not closed out. Meta's Messenger Desktop RN usage — **[?]**, no primary source obtained.

### 1.5 The projects doing this *right now* — and you must reckon with these

Three live 2026 projects occupy your exact space:

| Project | What it is | Stars | Created | Last push | License |
|---|---|---|---|---|---|
| **`gtkx-org/gtkx`** | React reconciler → GTK4/libadwaita GObjects via a native **Rust** FFI core. Node runtime, Fast Refresh, CSS-in-JS. **v1.6, self-describes production-ready.** Uses **GTK layout, not Yoga.** No RN compat claim. | **458** | — | **2026-08-31** | MPL-2.0 |
| **`itsmepetrov/react-native-gtkx`** | **The RN API on GTK4/Adwaita**, built on gtkx + **Yoga**. `npx react-native run-linux`. react-navigation backed by real `Adw.NavigationView`. Metro toolchain. npm v0.4.0. | 8 | 2026-07-30 | 2026-08-18 | MIT |
| **`lucid-softworks/react-native-linux`** | **C++** (654 KB) + TypeScript + CMake. Expo/expo-router shims, Fabric `createInstance`. 390 commits by one developer (`ImLunaHey`). | 31 | **2026-05-26** | 2026-08-11 | — |

[all V, via `gh api`]

`react-native-gtkx` is architecturally a **JS-level reimplementation** of the RN API — a custom reconciler with synchronous in-process FFI into libgtk, Yoga for layout, Node for "native modules." It does **not** use Fabric or ReactCommon [V, [architecture docs](https://itsmepetrov.github.io/react-native-gtkx/docs/architecture/overview)]. `lucid-softworks/react-native-linux` is the one that looks like a real C++/Fabric attempt — **and it already holds the GitHub name.**

---

## 2. What RN 0.87 requires from an out-of-tree platform

### 2.1 Version and architecture baseline [V]

0.76 (2024-10-23) made New Arch default → 0.80 froze Legacy → **0.81 was the last version able to run Legacy** → 0.82 (2025-10-08) is New-Arch-only → 0.83 began deleting Legacy code → 0.84 (2026-02-11) made **Hermes V1** the default engine → 0.87 (2026-08-11) made the **Strict TypeScript API** default, turning deep imports of `react-native/Libraries/*` into type errors (opt-out available only through 0.88). Paper is not fully gone — the **interop layers stay "for the foreseeable future"** per [reactwg discussion #309](https://github.com/reactwg/react-native-new-architecture/discussions/309). **A new platform should target New Arch only and never implement interop.**

### 2.2 The decisive discovery: `ReactCxxPlatform`

RN ships **`packages/react-native/ReactCxxPlatform/`** — 98 files, a complete generic C++ host platform. It appeared between 0.78 (absent) and 0.80 (present) and was last touched **2026-08-19** [V]. It provides:

`ReactHost` (with `createReactInstance`, `loadScriptFromDevServer`, `loadScriptFromBundlePath`, `startSurface`, `openDebugger`) · `PackagerConnection` + `DevServerHelper` (**Metro, Fast Refresh, LogBox, CDP inspector**) · `SchedulerDelegateImpl` · `IMountingManager` · `PlatformTimerRegistryImpl` · `MessageQueueThreadImpl` · HTTP + WebSocket clients · networking, image-loader, websocket modules · AppState / DeviceInfo / PlatformConstants core modules · perfetto + tracy profiling.

`ReactCxxTurboModuleProvider` hands you, for free: AppState, DeviceInfo, PlatformConstants, ImageLoader, Networking, WebSocket, SourceCode, DevSettings, DevLoadingView, LogBox, ExceptionsManager, IntersectionObserver, MutationObserver, Animated — plus the `DefaultTurboModules` set [V].

**The entire host contract is `IMountingManager`, which has exactly two pure-virtual methods** [V, source read]:

```cpp
virtual void executeMount(SurfaceId, MountingTransaction&&) = 0;
virtual void dispatchCommand(const ShadowView&, const std::string&, const folly::dynamic&) = 0;
```

Everything else is defaulted — including first-class accessibility hooks (`initializeAccessibilityManager`, `setAccessibilityFocusedView`, `accessibleClickAction`, `accessibleScrollInDirection`, `accessibleSetText`) and `getImageLoader()` / `getComponentRegistryFactory()`. **A11y is designed into the C++ seam.**

Alongside it, `react/renderer/*/platform/cxx` exists for **graphics, view, text, scrollview, modal, imagemanager, textlayoutmanager** — and this is *production* code, not test scaffolding: iOS itself compiles `components/view/platform/cxx` via `React-Fabric.podspec` [V]. Selection is automatic: `react-native-platform-selector.cmake` picks `platform/android` when `ANDROID` is defined and `platform/cxx` otherwise, so **a non-Android CMake build gets the C++ host platform for free** [V]. `ReactCommon` carries **86 CMakeLists.txt** files [V].

### 2.3 Meta already runs this on Linux

`private/react-native-fantom/` is RN's C++-hosted test runner. Its CMakeLists has an explicit desktop-Unix branch (`if(UNIX AND NOT APPLE)`, `-latomic`, and the comment *"Boost in NDK is not compatible with desktop build"*), and links **the entire renderer**: `react/renderer/{core,mounting,scheduler,uimanager,components/*,textlayoutmanager,attributedstring,graphics,animated,runtimescheduler}`, `react/runtime`, `yoga`, plus all twelve `ReactCxxPlatform` subdirs [V, source read].

Its CI action installs `cmake openssl libssl-dev clang`, builds with `CC=clang`, produces **`libhermesvm.so`**, and runs on `8-core-ubuntu` on **every PR** [V]. Hermes's own repo confirms this independently: `.github/workflows/build-hermesc-linux.yml` and release asset `hermes-cli-linux.tar.gz` [V]. **Hermes on desktop Linux is not a question mark.**

Engine choice is a clean seam anyway: `JSRuntimeFactory.h` + `JSIRuntimeHolder` accept **any `jsi::Runtime`**, with a non-Hermes fallback inspector delegate; `ReactCommon/jsc/JSCRuntime.cpp` still ships in 0.87 [V]. (V8/QuickJS/`hermes-windows` comparisons — **[?]**, not closed.)

### 2.4 The two real C++ gaps

Both verified by reading source:

1. **`TextLayoutManager` (cxx) is a stub.** Its `measure()` returns `layoutConstraints.minimumSize` and zero-sized attachments. No shaping, no measurement, nothing [V]. The contract is one virtual method, and `TextLayoutManagerExtended.h` uses C++20 concepts to make `measureLines()`/`prepareLayout()` *optional* — so you can ship with `measure()` alone.
2. **`ImageManager` (cxx) has every method commented `// Not implemented.`** [V].

Plus one hard absence: **`components/textinput` has no `platform/cxx` at all** — only `android` and `ios` [V]. TextInput must be authored from scratch.

Everything else in the minimal component set is **already hand-written C++ in-tree, requiring no codegen**. `StubComponentRegistryFactory.h` is literally the answer to "what is the minimal surface":

```cpp
providerRegistry->add(concreteComponentDescriptorProvider<ViewComponentDescriptor>());
providerRegistry->add(concreteComponentDescriptorProvider<ParagraphComponentDescriptor>());
providerRegistry->add(concreteComponentDescriptorProvider<RawTextComponentDescriptor>());
providerRegistry->add(concreteComponentDescriptorProvider<TextComponentDescriptor>());
providerRegistry->add(concreteComponentDescriptorProvider<ScrollViewComponentDescriptor>());
providerRegistry->add(concreteComponentDescriptorProvider<ImageComponentDescriptor>());
providerRegistry->add(concreteComponentDescriptorProvider<ModalHostViewComponentDescriptor>());
```

**Net new C++ work:** an `IMountingManager` implementation, a `TextLayoutManager`, an `IImageLoader`, an `EventBeat`/`RunLoopObserverManager` bound to your main loop, a `TextInput` component, and pointer/keyboard → `PointerEvent` injection (hit-testing is already done for you by `PointerEventsProcessor.h`). **Copy `private/react-native-fantom/tester/` — it is the de-facto host-platform template.**

### 2.5 Codegen is not a blocker

`RNCodegen.js` exposes generators as a **named, overridable map**, not a hardcoded platform switch — `LibraryOptions` accepts `libraryGenerators?: LibraryGeneratorsFunctions` [V]. The platform-neutral subset already exists: `descriptors`, `events`, `props`, `states`, `shadow-nodes`, **`modulesCxx`** — exactly the pure-C++ artifacts a C++ host needs, no JNI, no ObjC++. There is even an in-source `// TODO: Refactor this to consolidate various C++ output variation instead of forking per platform.` [V]. RN's own C++ platform currently just reuses the Android codegen output [V].

### 2.6 Registration and the JS surface

`react-native.config.js` `platforms` is an **open map** — `cli-types` declares `platforms: { android; ios; [name: string]: PlatformConfig<...> }` and `UserDependencyConfig.platforms` is documented as *"An array of extra platforms to load"*, requiring `npmPackageName?`, `projectConfig()`, `dependencyConfig()` (the autolinking hook) [V]. Metro's `@react-native/metro-config` defaults `resolver.platforms` to `['android','ios']`; you append `'linux'`, and resolution becomes `Foo.linux.js` → `Foo.native.js` → `Foo.js` [V].

The **official docs are unusable** — [reactnative.dev/docs/out-of-tree-platforms](https://reactnative.dev/docs/out-of-tree-platforms) still documents RN 0.57-era **RNPM + Haste** (`rnpm.haste.providesModuleNodeModules`), and Haste was removed from Metro years ago [V, raw markdown read].

**The JS surface is small but requires core patching.** Only **23 files** in `Libraries/` are platform-suffixed (11 `.android.js`, 12 `.ios.js`); there are **zero `.native.js`** files [V]. The `.linux.js` set you'd need: `Platform`, `BaseViewConfig`, `PlatformColorValueTypes`, `Image`, `RCTNetworking`, `RCTAlertManager`, `BackHandler`, `legacySendAccessibilityEvent`, `DrawerLayoutAndroid`, `ToastAndroid`, `ProgressBarAndroid`, `Settings`, `ReactDevToolsSettingsManager`. **Critically, the unsuffixed siblings are not generic fallbacks** — `Platform.js` is a self-referential re-export shim, so under `--platform linux` it resolves to itself. **`.linux.js` files must physically exist inside `node_modules/react-native/Libraries/`, which means patch-package or platform-override forever** [V mechanism, I on there being no supported alternative].

### 2.7 Two sharp caveats on `ReactCxxPlatform`

1. **It is not published to npm.** `npm pack react-native@0.87.1 --dry-run` ships 4,471 files including all 18 `platform/cxx` files and `cmake-utils`, but **zero** files from `ReactCxxPlatform` [V]. You vendor it from git and re-vendor every release.
2. **It currently reports itself to JS as Android.** `PlatformConstantsModule::getConstants()` returns a `PlatformConstantsAndroid` struct hardcoding `Version = 33 // Android 13`, and `DeviceInfoModule` hardcodes `1280x720` with `// TODO: Wire this to come from the actual app size` [V, source read]. So you face an early fork in the road: **masquerade as `Platform.OS === 'android'`** (what RN's own C++ platform does — correct third-party library behaviour, wrong semantics) **or add a true `linux` platform** (correct semantics, ~13 patched core JS files forever).

---

## 3. Rendering / windowing strategy on Linux

The renderer choice is the easy part. **Accessibility and IME are the killers**, and the asymmetry between options is stark.

### (a) Skia into a Wayland/X11 window

The reference implementation is Flutter's Linux embedder — and it is more dated than expected. Flutter still requires **GTK3** (`libgtk-3-dev`; `config/BUILD.gn` pins `gtk+-3.0`), renders **Skia + OpenGL** with a software fallback, has **no Vulkan and no Impeller** on Linux, and the GTK4 migration ([#94804](https://github.com/flutter/flutter/issues/94804)) has been open since December 2021 without landing. Impeller-on-Linux is a **design-doc issue opened 2026-03-11**, still a draft PR [S]. After ~7 years, Flutter desktop Linux is Skia+GL with acknowledged shader jank.

Skia bindings are healthy in Rust (`skia-safe` v0.99.0, 2026-06-19, prebuilt binaries, tracking Skia m152/m153) but Skia itself is **GN/Ninja-only, Clang-strongly-preferred, and makes no API/ABI stability commitment** — no distro ships a usable libskia, so you vendor and pin permanently [S].

**Sharpest caveat: "use Skia" does not give you a text stack.** Neither Chromium nor Flutter uses SkShaper. Flutter's `libtxt` defaults to **Minikin** (Android's HarfBuzz+ICU glue); SkShaper/SkParagraph is only an alternate backend behind `--enable-skshaper` [S]. Both major Skia consumers built their own text integration layer.

### (b) GTK4 native widgets

**One assumed blocker is false.** GTK4's `GtkFixedLayout` places children at absolute positions *and* `GtkFixedLayoutChild` exposes a `transform` property taking a `GskTransform` [S, [docs.gtk.org](https://docs.gtk.org/gtk4/class.FixedLayout.html)]. Yoga → `(x,y,w,h)` + transform is mechanically expressible. GTK's docs discourage `GtkFixedLayout` on theme/RTL/i18n grounds — but every one of those objections assumes GTK owns layout, which it wouldn't.

GTK4 gives you AT-SPI (ATK was dropped; `GtkATContext` proxies to the platform API), `GtkAccessibleText` since 4.14 with caret/selection notifications and `gtk_accessible_announce()`, `GtkIMContext` for IME, fontconfig, fractional scaling, portals, and theming — **free** [S]. The catch: free only for *stock* widgets. Custom widgets must implement `GtkAccessible` vfuncs by hand, and *"a role is a promise"* — declaring `BUTTON` obliges you to provide button keyboard semantics. If every RN host component is a custom widget you're wiring a11y manually regardless — but against a mature bridge rather than a from-scratch one.

**No project has mapped a flexbox engine onto GTK widgets** [S, negative result]. Notably, GTKX — the live, mature React-on-GTK4 project — *deliberately* uses GTK layout instead of flexbox. That is a signal worth taking seriously.

### (c) Qt6 — licensing is the real cost

The modules you'd need (Core, Gui, **Quick**, Quick Controls, Widgets, Network) are **LGPLv3** [S, [doc.qt.io/qt-6/licensing.html](https://doc.qt.io/qt-6/licensing.html)]. But:

- **Static linking is effectively off the table.** Qt's own FAQ warns it *"may cause your application to lose this protection."* LGPLv3 §4 requires shipping Corresponding Application Code in a form permitting users to **relink against a modified Qt**.
- **LTS point releases are commercial-only** since Qt 5.15. **Qt 6.8 LTS releases 6.8.4–6.8.9 are commercial-only** as of Sept 2026 — open-source consumers get no security patches on the LTS branch [S].
- The GPLv3-only module list (Qt Quick 3D, Qt Virtual Keyboard, Qt Qml Compiler, Qt Wayland Compositor, …) is deliberate policy; The Qt Company stated on its dev list that the goal is that *"those making closed-source applications or devices must pick the commercial option"* [S].
- **Qt6 still defaults to `text-input-v1` on Wayland**; `zwp_text_input_v3` requires opting in via env var [S].

For desktop Linux distribution (Flathub/Snap) LGPL is fine — the VLC/App-Store problem doesn't apply. But you would be pushing a relinking obligation onto every downstream closed-source app built on an otherwise-MIT platform.

**The best flexbox-on-native-widgets precedent is on Qt, not GTK:** **NodeGui** is MIT, Qt6, and implements **full flexbox via Yoga** — literally RN's own engine — as a custom `FlexLayout` QLayout [S]. That is the existence proof.

### (d) wgpu / Vello

**wgpu: production-grade, and specifically the Linux answer.** Zed migrated GPUI's Linux renderer from its custom Blade to **wgpu, merged 2026-02-13** ([PR #46758](https://github.com/zed-industries/zed/pull/46758)), citing NVIDIA freezes and Smithay-compositor breakage, and calling wgpu *"the de-facto standard in the rust UI and graphics ecosystem"* — while **keeping native renderers on Windows and macOS** [S]. Caveats: the GLES backend is in poor shape (effectively Vulkan-or-llvmpipe), pipeline caching is still an open issue, and COSMIC still carries an open NVIDIA/Wayland meta-issue in 2026 [S].

**Vello: not ready, and nobody ships production UI on it** [S, negative result]. `vello_hybrid` was *"roughly beta quality"* per Linebender's own 2026-Q1 report; its README documents features that **panic** rather than degrade. Bevy declined it as immature; Dioxus's Vello-based Blitz is self-described *"not recommended for use"*; Xilem is alpha; Masonry is mid-refactor with **no precedent of being driven by a foreign tree**.

On text, **`harfrust`** is a genuine bright spot — a Rust HarfBuzz rewrite under the official `harfbuzz` org, tracking HarfBuzz v13.0.0, <25% slower than C, with upstream HarfBuzz adding a differential-testing mode against it [S]. But color emoji was split into `Glifo`, still Vello-coupled and not a stable capability [S].

### (e) react-native-web in a system webview — the pragmatic tier 0 is weaker than it looks

**Tauri's own maintainers say WebKitGTK is the weak leg.** From [tauri#8524](https://github.com/tauri-apps/tauri/discussions/8524), core maintainer FabianLars: *"webkitgtk is unusable"* … *"with webkitgtk getting worse/more unstable each release i changed my mind."* As of **2026-01-13** CEF-on-Linux is still unshipped [S]. `WEBKIT_DISABLE_DMABUF_RENDERER=1` remains the **officially documented workaround** for blank windows on NVIDIA [S, [v2.tauri.app](https://v2.tauri.app/develop/debug/linux-graphics/)].

**And react-native-web is in maintenance mode** — Software Mansion's 2026 outlook states RNW *"has entered maintenance-only mode… no major features on the horizon"*; Nicolas Gallagher's investment moved to **React Strict DOM**, which targets Fabric natively [S]. RNW never touches Fabric at all; it runs through ordinary `react-dom` [I].

You also lose Hermes bytecode entirely (JSC in WebKitGTK), and the two headline "free wins" are both qualified: WebKitGTK's GTK4 a11y bridge is only ~2 years old (first stable in 2.44, 2024), and **fcitx CJK input is reproducibly broken in Epiphany itself — GNOME's own first-party WebKitGTK browser — as of WebKitGTK 2.44** [S].

### Cross-cutting killer #1 — accessibility

AT-SPI2 is the standard and is architecturally obsolete; Matt Campbell (AccessKit lead, now on GNOME a11y) has written that its performance is *"severely limited by the latency of multiple IPC round trips."* Its replacement, **Newton** (push-based, Wayland-native, explicitly built on AccessKit), was last given a detailed status update in **June 2024** — Orca *"basically usable on Wayland with some real GTK 4 apps"*, still missing GNOME Shell integration and terminal text widgets. **No 2025/2026 status post exists.** Newton's current state is a material ADR risk **[?]** [S].

**AccessKit** is the answer for custom-rendered stacks and it is real: `accesskit_unix` **v0.23.0, 2026-08-29**, BSD-3-Clause, AT-SPI over zbus, adopted by **Bevy, egui, Freya, Slint, Xilem**, with **C bindings** via cbindgen (so a C++ RN platform can consume it directly) [V, repo read]. But the `Document` and `EditableText` AT-SPI interfaces **landed one month ago**, and rich text/hypertext remain unsupported by AccessKit's own admission [S/V].

Two sobering data points: **Flutter has an ATK implementation but Linux/Orca is not on its supported assistive-technology list** [S]. And **Zed shipped 1.0 in April 2026 with no screen-reader support at all** — its plan is still *"integrate AccessKit into GPUI,"* a project it estimates will last *"far beyond 1.0"* [S].

### Cross-cutting killer #2 — IME

The fcitx5 maintainer is explicit: GTK/Qt users need minimal effort; **for a raw surface you implement `text-input-unstable-v3` yourself** [S]. The compositor matrix is uneven — GNOME/mutter supports `text-input-v3` app-side but **not `input-method-v2`, so fcitx5's candidate popup cannot render without the `kimpanel` shell extension**; KDE/kwin supports both; Sway has an open bug where Chromium's v3 client fails to commit [S]. ibus on Wayland only speaks `input_method_v1`, so on wlroots it effectively only helps XWayland apps [S].

You must implement: preedit rendering, `set_cursor_rectangle` tracking, `set_surrounding_text`, forwarding **all** key events including release, commit-string handling — **plus a separate XIM path** for X11/XWayland. The bug lists prove how hard this is: **egui has no working Linux IME** (*"unlikely to land soon"*), **iced has had it open since 2021** with *"no plan yet"*, **winit's `set_ime_cursor_area` is a no-op on Wayland**, and Alacritty and Zed have each shipped multiple IME regressions [S].

### Cross-cutting #3 — text and emoji

`wp-fractional-scale-v1` is supported across wlroots/KDE/GNOME [S]. The concrete landmine is emoji: Noto ships in two incompatible formats (CBDT bitmaps, COLRv1 vectors); **Fedora defaults to COLRv1, and renderers lacking COLRv1 silently render transparent glyphs rather than falling back** — an open bug affecting COSMIC Terminal, COSMIC Edit *and Zed* [S, [cosmic-epoch#2546](https://github.com/pop-os/cosmic-epoch/issues/2546)]. Also: skipping fontconfig means silently ignoring users' `fonts.conf` rules.

---

## 4. Ecosystem and naming

**The npm name is taken, but weakly.** `react-native-linux` exists: **v0.0.1, published 2018-06-25**, owner `bukharim96`, MIT, **3 files / 1,598 bytes unpacked**, `"description": ""`, **1 download in the last week**, and its declared repo `github.com/bukharim96/react-native-linux` now **404s** [V, npm registry + gh]. This is a textbook abandoned-placeholder case — npm's dispute policy is a plausible route, but **[?]** on outcome. Other names: `react-native-gtk` (2017, dead), `react-native-desktop` (2016, dead), `proton-native` (2020, dead) — all squatted-by-abandonment. **`react-native-wayland` and `react-native-x11` do not exist** [V]. `react-native-gtkx` is live and MIT [V].

**The GitHub name is taken and active** — `lucid-softworks/react-native-linux`, 31 stars, C++, last push 2026-08-11 [V]. Plus two dead 2017 Qt forks of the same name [V].

**Naming rules.** The official doc still specifies the module-name patterns `react-native-example`, `@org/react-native-example`, `@react-native-example/module` [V]. The trademark situation **changed fundamentally in February 2026**: React Native is no longer Meta's, it is the **React Foundation's** under Linux Foundation trademark policy [V]. Any naming/branding question now routes through LF Projects, not Meta's opensource.fb.com policy. **[?]** — I did not obtain the Foundation's specific trademark-usage guidelines, and **this should be closed before committing to a name.**

**RFCs.** I found **no accepted RFC in `react-native-community/discussions-and-proposals` for Linux support or for generalized out-of-tree platform extensibility**, and no official Meta/Foundation statement on Linux [S, **[?]** — this is the least-closed area of the report]. The absence is itself a finding: **there is no sanctioned path, no template, and stale documentation.** Note though that the strongest possible circumstantial signal exists — Amazon, Huawei, Microsoft, Callstack and Software Mansion, all out-of-tree platform builders, are now **platinum members of the foundation that owns React Native** [V].

---

## 5. Effort calibration

### What it cost the teams that did it

| Effort | Team | Duration | Outcome |
|---|---|---|---|
| **react-native-skia** (Linux/Skia/Fabric) | ~8–10 NAGRA/OpenTV engineers + Kudo Chien | 2020-09 → 2023-03 (~2.5 yr) | **Proof of concept**, RN 0.71, then abandoned [V] |
| **status-im RN-desktop-qt** | Status.im, small team, on Canonical's base | 2016 → 2021-02 (~4½ yr) | Working Qt port; *"support stopped"*; company rewrote natively [V] |
| **Amazon Vega/Kepler** | Amazon org-scale + Software Mansion + partners | Kepler → Vega launch = **20 months** for a *partner app*; the platform itself much longer | **Shipping commercial OS** [V/S] |
| **Huawei RNOH** | Huawei + Software Mansion | ~2023 → ongoing | **Shipping**, 1,156 commits, 54 releases, **290 forked libraries**, still 5 minors behind [V/S] |
| **RNW** | Microsoft, full-time | 2015 → ongoing, 11 yr | 5.6 MB C++, **798 open issues**, permanently 3 minors behind [V] |
| **RN-macOS** | Microsoft, full-time | 2018 → ongoing | Permanently **6 minors** behind [V] |
| **RN-visionOS** | Callstack (top-tier agency) | 2023-09 → **stalled 2025-09** | Abandoned after ~2 years [V] |
| **`lucid-softworks/react-native-linux`** | **1 developer** (`ImLunaHey`, 390 commits) | 2026-05-26 → present (~3 mo) | 654 KB of C++, Expo shims, Fabric work [V] |

### Sizing the target

For scale: RN's platform-specific native layers are **163 ObjC/ObjC++ files** (iOS) and **844 Java/Kotlin files** (Android), against **1,225 ReactCommon C++ files of which 703 are the platform-agnostic renderer you get for free** [V]. The JS surface is **23 platform-suffixed files** [V].

### Realistic tiers

**Tier 0 — webview-hosted (RNW + Tauri/WebKitGTK).** ~**1–2 engineer-months** to a shippable app. But: RNW is in maintenance mode, Tauri's maintainers call WebKitGTK unusable, you exercise zero Fabric/JSI/Hermes, and you build nothing reusable for later tiers. **This is a product shortcut, not a step on the path.** Its main honest use is buying time.

**Tier 1 — Skia-minimal, the 6 core components.** The `ReactCxxPlatform` discovery collapses this estimate dramatically. You are not writing a host — you are writing an `IMountingManager` (two pure virtuals), a `TextLayoutManager`, an `IImageLoader`, an event beat, and a `TextInput`. Metro, Fast Refresh, LogBox, CDP debugging, HTTP, WebSockets, timers, threading and 20 TurboModules come free. Estimate **3–6 engineer-months for a strong C++ engineer to a credible demo** — and the single existing data point supports this, since one developer got `lucid-softworks/react-native-linux` to 654 KB of C++ in three months. Bounded by: text measurement is the hard part, and `ReactCxxPlatform` must be vendored from git.

**Tier 2 — production (a11y, IME, ecosystem).** This is where every prior project died. NAGRA spent ~2.5 years with ~10 engineers and never got here. Zed, better funded than you will be, shipped 1.0 without a screen reader. **Estimate 3–10 engineer-years**, dominated not by rendering but by: AT-SPI (via AccessKit, with known gaps), IME across mutter/kwin/wlroots plus an XIM path, COLRv1 emoji and fontconfig fallback, mixed-DPI multi-monitor, and — the largest single line item — **forking the third-party library ecosystem**, which is what RNOH's 290 repositories actually represent.

**The permanent tax, regardless of tier:** RNW is 3 minors behind with a full-time Microsoft team; RN-macOS is 6; RNOH is 5. Assume **a 3–6 minor-version lag forever**, plus core JS patching forever (because `Platform.js` is a self-referential shim), plus re-vendoring `ReactCxxPlatform` from git every release (because it isn't on npm).

---

## Decision-relevant facts — the 10 most likely to change your architecture

1. **RN ships a generic C++ host platform, `packages/react-native/ReactCxxPlatform/` (98 files), and the entire host contract `IMountingManager` has only two pure-virtual methods** (`executeMount`, `dispatchCommand`), with accessibility hooks already defaulted in. You get `ReactHost`, Metro/Fast Refresh, LogBox, the CDP inspector, HTTP, WebSockets, timers, threading, and ~20 TurboModules for free. **[V]**

2. **Meta already builds and runs Fabric + Hermes + Yoga + bridgeless on x86-64 Ubuntu Linux in CI on every PR** (`react-native-fantom`, `libhermesvm.so`, `8-core-ubuntu`, explicit `if(UNIX AND NOT APPLE)` CMake branch). Hermes-on-Linux is a solved problem, and `private/react-native-fantom/tester/` is a ready-made host template. **[V]**

3. **Text is the gap, not rendering.** The `cxx` `TextLayoutManager` is a stub returning `layoutConstraints.minimumSize`; the `cxx` `ImageManager` is `// Not implemented.`; and **`components/textinput` has no `cxx` platform at all.** Compounding this: neither Chromium nor Flutter uses SkShaper — choosing Skia does *not* hand you a text stack. **[V/S]**

4. **`ReactCxxPlatform` is not published to npm** (0 of 4,471 files in the tarball) and **currently identifies itself to JS as Android** (`PlatformConstantsAndroid`, `Version = 33`). You vendor from git every release, and you must decide early whether to masquerade as `Platform.OS === 'android'` or pay for a true `linux` platform. **[V]**

5. **Core JS patching is unavoidable and permanent.** `Platform.js` and its siblings are self-referential re-export shims, not generic fallbacks, so `.linux.js` files must physically exist inside `node_modules/react-native/Libraries/`. This is exactly why Microsoft built `react-native-platform-override`. The surface is small (23 files) but the obligation is forever. **[V]**

6. **Out-of-tree platforms run a permanent 3–6 minor-version lag, even with vendor funding** — RNW 0.84 vs core 0.87 (Microsoft, full-time); RN-macOS 0.81.9; RNOH on 0.72/0.77 targeting 0.82 (Huawei + Software Mansion). And Callstack's react-native-visionOS — the easiest possible out-of-tree platform — **has been stalled since September 2025**. **[V]**

7. **Three live projects already occupy this space, and both names are taken.** `lucid-softworks/react-native-linux` (C++/CMake, 390 commits by one dev, active 2026-08-11) holds the GitHub name; npm `react-native-linux` is a dead 1.6 KB 2018 stub with 1 weekly download; and `itsmepetrov/react-native-gtkx` already ships `npx react-native run-linux` on GTK4 + Yoga. Evaluate collaboration before duplication. **[V]**

8. **Accessibility is the documented graveyard.** Flutter has an ATK implementation but **Linux/Orca is not on its supported-AT list**; **Zed shipped 1.0 in April 2026 with no screen reader**; GNOME's AT-SPI replacement (Newton) has had **no status update since June 2024**. AccessKit's `accesskit_unix` is real and has C bindings, but its `EditableText`/`Document` AT-SPI interfaces landed **one month ago** and rich text is still unsupported. Choosing GTK4/Qt inherits a mature bridge; choosing Skia/wgpu means attempting what Zed has not managed. **[V/S]**

9. **IME is the second graveyard, and it is toolkit-determined.** GNOME/mutter does not implement `input-method-v2`, so fcitx5 candidate popups need a shell extension; ibus on Wayland only helps XWayland on wlroots; **egui and iced both have unresolved multi-year Linux IME failures**, traceable to winit's `set_ime_cursor_area` being a no-op on Wayland. GTK4's `GtkIMContext` and Qt make this free; a custom surface means implementing `text-input-v3` *and* a separate XIM path. **[S]**

10. **The governance ground shifted on 2026-02-24**: React Native is now owned by the **React Foundation** (Linux Foundation), whose platinum members include **Amazon, Huawei, Microsoft, Callstack and Software Mansion — every major out-of-tree platform builder**. Simultaneously, the official out-of-tree docs are ~8 years stale (still documenting RNPM/Haste) and **no RFC for Linux or generalized platform extensibility exists**. There is no sanctioned path — but there has never been a better-aligned set of stakeholders to create one. **[V/S]**

---

### Open items I would close before finalizing the ADR

- **React Foundation trademark/naming policy** for `react-native-*` projects — genuinely unresolved, and it gates the name. **[?]**
- **`react-native-platform-override` 2026 maintenance status**, and whether an alternative to core patching has emerged. **[?]**
- **Direct contact with `lucid-softworks/react-native-linux`** — three months of solo C++ work in your exact problem space is either a collaborator or a reason not to start.
- **Amazon Vega's actual RN version** (docs say 0.72, npm reportedly 0.83) and whether any of RNV is open-sourceable given Amazon's foundation membership. **[?]**
- **Four hands-on spikes** the documentation cannot answer: Orca against each candidate toolkit; fcitx5/ibus CJK on GNOME + KDE + Sway; COLRv1 emoji on Fedora; mixed-DPI multi-monitor with output-integer and surface-fractional scale kept separate (Zed's live bug is the template for what not to do).

**Sources:** [react/react-native](https://github.com/react/react-native) · [ReactCxxPlatform](https://github.com/react/react-native/tree/main/packages/react-native/ReactCxxPlatform) · [react-native-fantom](https://github.com/react/react-native/tree/main/private/react-native-fantom) · [Out-of-Tree Platforms doc](https://reactnative.dev/docs/out-of-tree-platforms) · [The React Foundation](https://react.dev/blog/2026/02/24/the-react-foundation) · [react-native-skia/react-native-skia](https://github.com/react-native-skia/react-native-skia) · [status-im/react-native-desktop-qt](https://github.com/status-im/react-native-desktop-qt) · [microsoft/react-native-windows](https://github.com/microsoft/react-native-windows) · [microsoft/react-native-macos](https://github.com/microsoft/react-native-macos) · [callstack/react-native-visionos](https://github.com/callstack/react-native-visionos) · [Vega RN architecture](https://developer.amazon.com/docs/vega/0.23/vega-rn-arch.html) · [Amazon × Software Mansion](https://swmansion.com/blog/amazon-x-software-mansion-powering-vega-os-with-react-native-75c4cf522fd7/) · [Huawei × Software Mansion](https://swmansion.com/blog/huawei-x-software-mansion-bringing-react-native-support-to-harmonyos-next-82e02bd75549/) · [gtkx-org/gtkx](https://github.com/gtkx-org/gtkx) · [itsmepetrov/react-native-gtkx](https://github.com/itsmepetrov/react-native-gtkx) · [lucid-softworks/react-native-linux](https://github.com/lucid-softworks/react-native-linux) · [AccessKit](https://github.com/AccessKit/accesskit) · [tauri#8524](https://github.com/tauri-apps/tauri/discussions/8524) · [facebook/hermes](https://github.com/facebook/hermes)
