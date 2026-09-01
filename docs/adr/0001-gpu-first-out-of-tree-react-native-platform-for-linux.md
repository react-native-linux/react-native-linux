# ADR-0001: Build react-native-linux as a GPU-first out-of-tree React Native platform for desktop Linux

- Status: Accepted (amended 2026-09-01 after adversarial review)
- Date: 2026-09-01
- Deciders: Vitalii Yehorov
- Flagship consumer: [Suuudokuuu](https://www.suuudokuuu.com) on [Omarchy](https://omarchy.org) (Hyprland/Wayland, Arch Linux)

## Context

React Native has first-class mobile targets and Microsoft-maintained desktop targets for Windows (`react-native-windows`) and macOS (`react-native-macos`). Desktop Linux has no maintained platform. Every historical attempt is dead: `status-im/react-native-desktop-qt` stopped at the commit literally titled "Project support stopped" (2021-02-16), `kusti8/proton-native` last shipped in January 2023, `Place1/react-native-gtk` in 2019.

### Prior art, stated honestly

The portability of the React Native core to Linux-family systems is real but narrower than it is usually presented. The corrected record:

- **`react-native-skia/react-native-skia`** (org repo, sponsored by NAGRA/OpenTV — **not** Microsoft; there is no `microsoft/react-native-skia`). This is the closest precedent for what we are building: RN rendered through Skia on Linux, with embedded work in `nagra-opentv/buildroot-react-native-skia`. Its own README says *"The project is still in proof of concept and requires lots of work"* and *"Only Linux (Ubuntu 18) is supported in the mean time."* It pinned RN 0.71.3 and Skia `chrome/m108`, shipped zero releases, still contained legacy native modules, and its last commit landed in March 2023. **It is a failed precedent, and it is the only direct one.** It must not be confused with `Shopify/react-native-skia`, an actively maintained Skia *drawing library for RN apps*, which is a different thing entirely.
- **Amazon Vega** ships React Native as the app platform of a Linux-based OS in production: Echo Show 5 (3rd gen), Echo Hub, Echo Spot, Fire TV Stick 4K Select, Fire TV Stick HD. Amazon's own docs describe it as *"an out-of-tree implementation"* that ported React Common, Yoga, and Hermes, on Fabric, currently supporting RN 0.72 and 0.83. This proves the **core is portable**. It does not validate our renderer: Vega draws through Amazon's own native UI framework over its compositor, and there is no public evidence that Skia is involved. ("Kepler" was the pre-launch codename and is no longer the product name.)
- **RNOH / react-native-openharmony** (`OpenHarmony-SIG/ohos_react_native`, Huawei with Software Mansion) is a genuine, actively maintained out-of-tree New Architecture platform with 0.72, 0.77, 0.82 stable lines and a 0.84 release candidate. It is also **counter-evidence to our thesis**: it mounts Fabric into ArkUI native components through the ArkUI C-API — a widget tree. Its default npm `latest` tag is still 0.72.
- **`react-native-windows`** is the model for out-of-tree platform *tooling* — platform registration, a parallel codegen, a JS platform package. Its reusable C++ is React Native's own `ReactCommon`; RNW's own Fabric mounting layer is bound to the Windows App SDK Visual Layer and is not portable.

**The pattern is unambiguous: every platform that shipped chose a native UI framework, and the one project that chose a GPU canvas died.** This ADR argues for the canvas anyway; the reasoning is in *Alternatives considered*, and the cost is in *Consequences*.

### Live projects on desktop Linux today

Three, not one:

- [`lucid-softworks/react-native-linux`](https://github.com/lucid-softworks/react-native-linux) — Fabric mounted onto GTK4 widgets, Hermes, New Architecture, targeting RN ^0.81, structurally modelled on `react-native-windows`. It is further along than the ADR's earlier draft credited. Working per its README: View, Text, Image, ScrollView, TextInput, Pressable, Switch, ActivityIndicator, Modal, FlatList/SectionList, RefreshControl; `Animated` with **both** a JS driver and a native driver (transform/opacity applied per frame through `gtk_fixed_set_child_transform`, coalesced to one `setNativeProps` per host per frame); Fast Refresh at ~135 ms edit-to-visible; 26 in-tree `expo-modules-core` shims; `react-native-paper` mounting and animating; AsyncStorage, Linking, Alert, Appearance, Dimensions. That is our M0, our M1, and most of our M2, delivered in roughly three months. Counterweights, also honest: one effective contributor, ~31 stars, no tags, no releases, nothing published to npm, and no GPU/Skia path.
- [`itsmepetrov/react-native-gtkx`](https://github.com/itsmepetrov/react-native-gtkx) — GTK4/Adwaita via a React reconciler and in-process FFI, `npx react-native run-linux`, npm-published, ships .deb artifacts, and demonstrates a real Hacker News application with native `Adw.NavigationView` navigation.
- [`clayrisser/react-gtk`](https://github.com/clayrisser/react-gtk) — a longer-running React-to-GTK bridge, active as of August 2026.

### Motivating requirement

A **native 120 fps rendering target** on Hyprland/Wayland (Omarchy). 120 Hz is an 8.33 ms display interval; the compositor must composite and hit its own flip deadline inside the same window, so the real client budget is a fraction of that and is not specified anywhere. The requirement is therefore stated as: **hit the compositor's deadline every frame, with correct FIFO semantics, and keep animation independent of the JavaScript thread.**

## Decision

Build our own out-of-tree platform, **differentiated by the renderer**: GPU-direct retained-mode rendering instead of a native-widget tree.

1. **Renderer: Skia over Vulkan**, drawing into a Wayland surface. **Ganesh is the M0 backend.** Graphite is a tracked target, not a first milestone: Skia's own `gn/skia.gni` ships `skia_enable_graphite = false`, Chromium's Graphite is Dawn-only (there is no `kGraphiteVulkan` in `GrContextType`), Chrome enables Graphite by default on Apple platforms only, skia.org carries no Graphite documentation, and Skia's Graphite-Vulkan Linux CI has no AMD coverage. Skia maintainers' most recent public position is that Graphite is "stable enough to start testing." Ganesh is on a stated deprecation path with no announced timeline; we accept that our M0 backend is a known dead end and design the renderer interface so the backend is swappable. `wgpu`/`vello` remains a tracked alternative behind that same interface.
2. **Retained scene with damage tracking.** A persistent scene diffs mounting transactions into minimal damage regions; per-frame cost is proportional to change, not scene size.
3. **Frame pacing from the compositor.** The mechanisms are distinct and none of them substitute for another:
   - `wl_surface.frame` callbacks **throttle**. The core protocol calls this a "frame throttling hint," its timestamp has an undefined base, and a compositor *should avoid signalling frame callbacks for a surface that is not visible*. Hyprland only sends frame events for the active and active-special workspaces and skips surfaces that are not alive and visible. **A window on an inactive workspace receives no frame callbacks at all, so a timer fallback is mandatory, not optional.**
   - `wp_presentation_feedback` **measures**. It is purely retrospective: it reports the realized presentation time after a flip completes, its `refresh` field is a prediction that may be zero, and it has no request that can target a future time. We bind version 2 (Hyprland zeroes the refresh hint for version 1 clients while VRR is active).
   - `wp_fifo_v1` and `wp_commit_timer_v1` **schedule**. These are the actual pacing primitives; both landed in wayland-protocols 1.38 and both are staging. Hyprland implements them since 0.53, with commit timing behind `render:commit_timing_enabled`. Note `fifo-v1`'s own caveat that clients must still use frame callbacks or timestamps to guarantee throttling under all conditions.

   **VRR and direct scanout are compositor policy and are not earned by good pacing.** Hyprland's `misc:vrr` defaults to off; `ensureVRR()` consults the config value, per-monitor rules, fullscreen state, `content-type-v1`, and hardware capability, and never inspects client pacing. Direct scanout requires a solitary, fullscreen, opaque, monitor-sized, DMA-BUF-backed surface with no subsurfaces, no transform, no active capture, and no visible top-layer surfaces — a visible status bar disqualifies it. A windowed application will not get direct scanout. We do not plan around either.
4. **New Architecture only.** Fabric + TurboModules + codegen from the first commit. No Paper, no legacy bridge.
5. **Hermes** as the JavaScript engine, **built from source per React Native version**. Linux is a supported Hermes build host with documented Arch instructions, but there is no released Linux runtime artifact (upstream CI builds `hermesc` for Linux, not the VM), and Hermes' own README warns that a Hermes/RN version mismatch can crash instantly. Accepted consequences: **no x86-64 JIT** (stock Hermes is AOT-plus-interpreter; Static Hermes is the upstream successor and is not something to plan around), and an **`Intl` gap** — Hermes' ECMA-402 support is documented for Android and iOS only, and the generic `PlatformIntlICU` path behind `HERMES_ENABLE_INTL` is undocumented and untested on Linux. Any future consideration of `v8-jsi` is gated on the M2 benchmark, not on preference.
6. **Threading and animation, in React Native's actual terms.** Our mounting layer receives a `MountingTransaction` carrying `ShadowViewMutation`s over the full five-operation set — `Create`, `Delete`, `Insert`, `Remove`, `Update` — computed upstream by `calculateShadowViewMutations` in C++. **We do not diff the shadow tree; Fabric does.** View flattening runs inside that diff, so shadow nodes are not one-to-one with our scene nodes. Yoga layout runs in the commit phase on whichever thread commits (JS, background, or UI, by event priority — React Native's own Threading Model and Render Pipeline documents disagree on this, and we treat the thread as unspecified). Mounting is applied on the UI thread. On top of that we add a platform-owned frame thread, which is our concept and not a React Native one. Animations are **native-driven from day one**: transform and opacity are applied without a JavaScript round-trip, so a JS-thread stall never drops an animation frame. We build on React Native's shared C++ animated node graph (`ReactCommon/react/renderer/animated/`) and the `animationbackend` surface rather than hand-rolling a graph as `react-native-windows` had to.
7. **Text via Skia SkParagraph** (HarfBuzz shaping, FreeType rasterization through fontconfig on Linux), with a paragraph-layout cache and GPU glyph atlas. Accepted: SkParagraph is undocumented by Skia — no README, no design doc, no stability statement — and it pulls SkUnicode/ICU, which measurably inflates binary size. `skia_use_libgrapheme` and ICU4X are the escape hatches to evaluate.
8. **Wayland-first.** X11 support only if it falls out of the windowing abstraction for free.
9. **True `linux` platform identity from day one.** `Platform.OS === 'linux'`, `.linux.tsx`/`.linux.ts` resolution, and `react-native-platform-override` adopted for the roughly thirteen core JS files that need platform overrides. This is a deliberate choice of the harder path: the React Native CLI's platform contract requires that the package named by `npmPackageName` *provide a complete React Native implementation for that platform*, which is why `react-native-windows` and `react-native-macos` are forks of the `react-native` JS package rather than plugins. We accept that obligation explicitly rather than shipping under a borrowed platform name. The recurring cost is recorded in *Additional accepted risks*.
10. **Naming.** GitHub org `react-native-linux`, created 2026-09-01; npm packages under the `@react-native-linux/*` scope (e.g. `@react-native-linux/core`), **publication deferred** until there is something worth installing. Two honest disclosures: (a) [`lucid-softworks/react-native-linux`](https://github.com/lucid-softworks/react-native-linux) already carries this repository name and predates us by roughly three months — the collision is real, we did not discover the name first, and we accept the resulting search and issue-tracker ambiguity as our problem to manage, not theirs; (b) the unscoped npm name `react-native-linux` holds a single 0.0.1 placeholder published in June 2018 by an unrelated author and is not used. "React Native" is a Linux Foundation trademark under Meta's stewardship; the name is descriptive of an out-of-tree platform in the established `react-native-windows`/`react-native-macos` pattern, and we will rename on request from the trademark holder rather than argue.

### Minimal platform surface (what "working" means)

`View`, `Text`, `Image`, `ScrollView`, `TextInput`, `Pressable` with pointer/keyboard events — the six components required to render a real application. **`TextInput` is not "working" without IME**: on a bare Wayland surface that means client-side `zwp_text_input_v3` with pre-edit rendering, cursor rectangles and surrounding-text sync, plus `xkbcommon` compose sequences, dead keys and key repeat. IME is therefore a prerequisite of M1, not a later milestone.

This surface is a demo, not a product. The definition of the *product* is in M4.

## Alternatives considered

### Contribute to lucid-softworks/react-native-linux (GTK4)

Rejected as the primary path, respectfully — and on narrower grounds than an earlier draft of this ADR claimed.

**Conceded, because it is true:** GTK 4.16 made the GSK **Vulkan** renderer the default on Wayland. GSK is already a retained scene graph with damage tracking. `GdkFrameClock` is vsync-driven and `gtk_widget_add_tick_callback` gives frame-clock-paced animation. GTK4 already performs DMA-BUF graphics offload for direct scanout, under the same fullscreen and full-coverage constraints the compositor would impose on us. **"GTK owns the frame loop and therefore caps the frame budget" was wrong and is withdrawn.** The ~30 fps figure visible in that project's notes comes from software cairo inside a VM over VNC, which their own notes identify as the cause; it is not a GTK ceiling. GTK4 also hands you window management, IME, and AT-SPI accessibility, which are three of our hardest problems.

**The remaining reasons, which we believe hold:**

- **Paint-model ownership.** React Native's visual semantics are defined by what iOS and Android do. Mapping them onto a third toolkit's painting model means every mismatch is a negotiation: per-corner `borderRadius` under asymmetric border widths, `overflow` clipping, shadow and elevation parity, text decoration, `resizeMode`, `transform` origin. On a canvas the mismatch cannot occur, because there is no second model.
- **Styling fidelity as a product requirement.** React Native's style surface is converging on CSS and is still growing — 0.76 added `boxShadow` and `filter`, 0.77 added `mixBlendMode`, `outline`, `boxSizing`, `display: contents`. Each addition is a first-class canvas operation and a per-widget workaround in a widget tree. We would rather own a growing spec than an ever-widening impedance mismatch.
- **A single cross-platform paint contract.** One renderer that produces identical pixels wherever it runs is a stronger long-term asset for React Native on Linux than a Linux-shaped adaptation layer, and it is the piece nobody has built and kept alive.

These are aesthetic-fidelity and architecture arguments, not performance arguments. The projects are complementary — widgets-integration versus GPU-canvas — and shared learnings, especially their `expo-modules` shims and Fabric mounting layer, are welcome in both directions.

### Fork lucid-softworks

Rejected: the renderer is the foundation, and it is the part we are replacing. A fork would carry GTK4 architecture debt plus hostile-fork optics for no reuse benefit.

### Host react-native-web in a system webview (wry/WebKitGTK)

Rejected as the platform strategy — it is not React Native on Linux, it is a browser. It remains a valid short-term distribution path for individual apps, and Suuudokuuu ships to Omarchy today as a webapp/AUR package, independently of this project.

### Qt-based renderer

Rejected: licensing complexity and the same paint-model ownership problem as GTK4.

## Consequences

Positive:

- Full ownership of the paint model makes React Native's styling semantics an implementation task rather than a per-widget negotiation.
- Full ownership of the frame loop makes pacing measurable end to end.
- Renderer-first scope gives a demonstrable vertical slice early: window → `View` → the six components → measured pacing on Hyprland.

Negative / accepted risks:

- **Text, IME, and accessibility are the graveyard of GPU-canvas toolkits, and we are choosing the path Flutter declined.** Flutter's Linux embedder is GTK-based (`libflutter_linux_gtk.so`) precisely because window management and IME are strongly coupled to GTK/GDK; it reaches AT-SPI through ATK, and the proposal to speak AT-SPI directly over D-Bus has been open and unfinished since November 2024. We are proposing to do from scratch what the reference GPU-canvas toolkit chose not to do, with fewer people. IME moves to M1 for this reason; AT-SPI2 over D-Bus — object tree, `Text`/`EditableText`/`Value`/`Selection` interfaces, focus and caret events, and a mapping from React Native's `accessibilityRole`/`accessibilityLabel` props that has no upstream precedent to copy — stays a first-class roadmap item rather than an afterthought.
- No native-widget fidelity: we draw everything, so we style everything.
- Ecosystem gap: TurboModule codegen/autolinking for a new platform is significant plumbing before third-party libraries work.
- Competing with active community projects splits attention; mitigated by genuinely different architecture and stated non-rivalry.

### Additional accepted risks

Recorded because they were previously absent, and each is load-bearing:

- **We take on a `react-native` JavaScript fork.** Decision 9's platform identity means maintaining a complete platform implementation package. `react-native-platform-override` is adopted to keep the roughly thirteen core JS overrides auditable and to make upstream drift visible as a diff rather than a surprise, but the obligation itself does not go away and grows with every React Native release.
- **We take on a parallel codegen driver.** React Native's codegen targets are a closed union hardcoded to Android, iOS, and C++ (`componentsAndroid`, `componentsIOS`, `modulesAndroid`, `modulesCxx`, `modulesIOS`, …), with an upstream `TODO` acknowledging the per-platform fork. `react-native-windows` ships an entire parallel codegen package for this reason; so will we. The official out-of-tree platforms documentation still describes RNPM and Haste and states outright that the process is not well documented.
- **Skia is vendored at a git hash, with no ABI and no releases.** Skia recommends tracking tip-of-tree, publishes no versioned native releases and no official prebuilt binaries, and requires depot_tools, C++20/Clang, and Bazelisk for build-file changes. Flutter vendors it by raw hash and maintains its own copies of module build files; Shopify ships prebuilt packages because source builds are slow enough to matter. Skia's own Vulkan documentation warns of driver bugs "for which we have no workaround," and Skia supplies no window-system integration at all — swapchain, Wayland surface creation, DMA-BUF import, presentation, and every barrier are ours. Note also that Suuudokuuu depends on `@shopify/react-native-skia`, so the flagship will run two Skia builds in one process until that is resolved.
- **ScrollView physics fidelity is hand-written and will feel wrong for a long time.** React Native's `ScrollView` semantics are `UIScrollView` semantics — `decelerationRate` "normal"/"fast" map to the literal iOS constants — plus rubber-band overscroll, `onMomentumScrollBegin`/`End`, `snapToInterval`, `pagingEnabled`, `contentInset`, `maintainVisibleContentPosition`, and the scroll-event cadence `FlatList` windowing assumes. A widget-tree platform gets a plausible approximation free from its scrolled-window widget; we get nothing free. Kinetic touchpad input and discrete wheel input are separate problems.
- **Fractional and multi-monitor DPI is a first-class subsystem.** `wp_fractional_scale_v1` (scale/120), `wl_surface.set_buffer_scale`, `wp_viewporter`, per-output scale changes when a window is dragged between monitors, glyph atlas invalidation and re-rasterization at the new scale, and hit-testing in the correct coordinate space. GTK4 and Qt6 each took years on this.
- **`Intl` is a Linux-specific hole.** Suuudokuuu already leans on `@formatjs/intl-pluralrules`, `expo-localization`, and Lingui `plural()`. Either the untested `PlatformIntlICU` path is brought up and verified against ECMA-402, or the polyfill route is made an explicit platform recommendation. `Intl.PluralRules`, `RelativeTimeFormat`, `ListFormat`, and `DisplayNames` are listed as planned upstream on every platform.
- **We plan to sit three to six minors behind React Native, permanently.** React Native ships roughly six minors a year (0.84 in February, 0.85 in April, 0.86 in June, 0.87 in August 2026) and supports the current plus previous two minors, about six months. The stability programme upstream covers the JavaScript API and says nothing about C++ or out-of-tree platforms; 0.81 shipped breaking C++ changes that broke downstream builds. Calibration, as of September 2026:

  | Package | Latest | Published | RN target | Lag behind RN 0.87.1 |
  | --- | --- | --- | --- | --- |
  | `react-native` | 0.87.1 | 2026-08-26 | — | — |
  | `react-native-windows` | 0.84.0 | 2026-06-18 | 0.84 | 3 minors |
  | `react-native-macos` | 0.81.9 | 2026-07-13 | 0.81 | 6 minors (an unsupported RN) |
  | `@react-native-oh/react-native-harmony` | 0.72.143 | 2026-08-18 | 0.72 | 15 minors |

  `react-native-windows` reaches that three-minor lag with hundreds of contributors, roughly 5.6 MB of C++, and a decade of history. We are proposing a larger surface — renderer, text, IME, accessibility, ecosystem — with far fewer people. Pinning to a stable React Native minor and upgrading deliberately is the plan, not a failure mode.

## Roadmap (milestones, each demonstrable)

- **M0** — Wayland window + Vulkan Skia (Ganesh) surface + Hermes built from source running a bundle; a `View` renders.
- **M1** — The six core components; Yoga layout; pointer + keyboard events; **`zwp_text_input_v3` IME so `TextInput` is genuinely usable**; fractional-scale handling for a single monitor.
- **M2** — Native-driven animations on the shared C++ animated node graph; damage tracking; pacing on `wp_fifo_v1` + `wp_commit_timer_v1` with a timer fallback, measured with `wp_presentation_feedback` on Hyprland; Tracy instrumentation. **This milestone also produces the Hermes performance baseline that gates any future engine reconsideration.**
- **M3** — Parallel codegen driver + TurboModule autolinking; CLI/Metro platform registration (`--platform linux`); `react-native-platform-override` in place for the core JS overrides.
- **M4** — **Ecosystem porting programme, not a single milestone.** Running Suuudokuuu means Linux implementations or shims for roughly thirty native dependencies: about twenty Expo modules (`expo-router`, `expo-sqlite`, `expo-updates`, `expo-font`, `expo-localization`, `expo-linking`, `expo-splash-screen`, `expo-constants`, `@expo/ui`, …), `react-native-screens`, `react-native-safe-area-context`, `react-native-gesture-handler`, `react-native-svg`, `react-native-nitro-modules`, `react-native-unistyles`, `@react-native-masked-view/masked-view`, `react-native-share`, and `@shopify/react-native-skia`. The gating decision inside M4 is **Reanimated**: `react-native-reanimated` and `react-native-worklets` ship `android/` and `apple/` native directories only — no Windows, no macOS, no OpenHarmony — and Reanimated 4 is New-Architecture-only. Supporting it means porting a second JSI UI runtime with its own worklet serialization boundary, wired into our mounting layer. Neither Microsoft nor Huawei has done this. M4 therefore begins with an explicit port-or-decline decision on Reanimated and a substitute animation story if we decline. Packaging (AUR, omarchy-pkgs) lands at the end of this programme.
- **M5** — AT-SPI2 accessibility over D-Bus; multi-window; hardening.

## Notes

This ADR was amended on 2026-09-01 following an adversarial technical review that corrected the prior-art record, the Wayland pacing claims, the Fabric and threading terminology, and the grounds on which the GTK4 alternative was rejected. Claims that did not survive verification were removed rather than softened; the specific reversals are the withdrawn "GTK caps the frame budget" argument, the withdrawn "VRR and direct scanout come for free" claim, and the corrected attribution and status of `react-native-skia`.

The full research record lives in `docs/research/`: `prior-art.md` (prior art and effort calibration), `js-engine-selection.md` (Hermes vs alternatives on Linux), and `codegen-and-oot-platform-tooling.md` (codegen, CLI, and platform-registration mechanics). Where those reports and this ADR disagree, the reports win and this ADR gets amended.
