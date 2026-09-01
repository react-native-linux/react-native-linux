# ADR-0001: Build react-native-linux as a GPU-first out-of-tree React Native platform for desktop Linux

- Status: Proposed
- Date: 2026-09-01
- Deciders: Vitalii Yehorov
- Flagship consumer: [Suuudokuuu](https://www.suuudokuuu.com) on [Omarchy](https://omarchy.org) (Hyprland/Wayland, Arch Linux)

## Context

React Native has first-class mobile targets and Microsoft-maintained desktop targets for Windows (`react-native-windows`) and macOS (`react-native-macos`). Desktop Linux has no maintained platform. Every historical attempt (react-native-desktop/Qt, Proton Native, react-native-gtk) is dead.

The portability of the React Native core to Linux-family systems is proven, not speculative:

- **react-native-skia** (`react-native-skia/react-native-skia`, started by Kudo Chien, NAGRA-funded, dormant since 2023) demonstrated RN rendering through Skia on Linux (X11/Wayland/DirectFB) and embedded targets.
- **Amazon Vega (Kepler)** ships React Native (ReactCommon, Yoga, Hermes, Fabric) as the app platform of a Linux-based OS in production retail devices; its rendering layer is undisclosed.
- **react-native-harmony** shows a vendor building a complete out-of-tree New Architecture platform.
- **react-native-windows** maintains a reusable cross-platform C++ core and the out-of-tree platform tooling patterns we can model.

One active community project exists: [`lucid-softworks/react-native-linux`](https://github.com/lucid-softworks/react-native-linux) — an alpha mapping Fabric onto **GTK4 native widgets** (GtkFixed/GtkLabel/GtkPicture), with Hermes and New Architecture from day one. It is real prior art and further along than any predecessor.

Our motivating requirement is a **native 120 fps rendering target** on Hyprland/Wayland (Omarchy): an 8.33 ms end-to-end frame budget, honest vsync pacing, and animation independent of the JavaScript thread.

## Decision

Build our own out-of-tree platform, **differentiated by the renderer**: GPU-direct retained-mode rendering instead of a native-widget tree.

1. **Renderer: Skia (Graphite) over Vulkan** as the primary backend, drawing Fabric's shadow tree directly into a Wayland surface. `wgpu`/`vello` remains a tracked alternative behind the same renderer interface, not a first milestone.
2. **Retained scene with damage tracking.** A persistent render tree diffs Fabric commits into minimal damage regions; per-frame cost is proportional to change, not scene size.
3. **Frame pacing from the compositor.** `wl_surface` frame callbacks plus `wp_presentation` timestamps drive the loop — no timers. VRR and direct scanout come for free on Hyprland when pacing is honest.
4. **New Architecture only.** Fabric + TurboModules + codegen from the first commit. No Paper, no legacy bridge.
5. **Hermes** as the JavaScript engine, with the RN threading model enforced: JS thread, background Yoga commit, dedicated render thread. Animations are **native-driven from day one** (transform/opacity applied on the render thread); a JS-thread stall must never drop an animation frame.
6. **Text via Skia SkParagraph** (HarfBuzz shaping, FreeType rasterization), with a paragraph-layout cache and GPU glyph atlas.
7. **Wayland-first.** X11 support only if it falls out of the windowing abstraction for free.
8. **Naming:** GitHub org `react-native-linux`; npm packages under the `@react-native-linux/*` scope (e.g. `@react-native-linux/core`). The unscoped npm name `react-native-linux` is squatted by an unrelated placeholder and is not used.

### Minimal platform surface (what "working" means)

`View`, `Text`, `Image`, `ScrollView`, `TextInput`, `Pressable` with pointer/keyboard events — the six components required to render a real application, validated by running Suuudokuuu.

## Alternatives considered

### Contribute to lucid-softworks/react-native-linux (GTK4)

Rejected as the primary path, respectfully. The GTK4 approach is impressive and pragmatic, but a native-widget tree cannot guarantee our core requirement: GTK owns the frame loop, widget invalidation granularity, and compositing, which caps control over the 8.33 ms budget and fine-grained damage. It also inherits GTK's styling model rather than RN's. The projects are complementary (widgets-integration vs GPU-canvas), and shared learnings — especially their expo-modules shims and Fabric mounting layer — are welcome in both directions.

### Fork lucid-softworks

Rejected: the renderer is the foundation, and it is the part we are replacing. A fork would carry GTK4 architecture debt plus hostile-fork optics for no reuse benefit.

### Host react-native-web in a system webview (wry/WebKitGTK)

Rejected as the platform strategy — it is not React Native on Linux, it is a browser. It remains a valid short-term distribution path for individual apps (Suuudokuuu ships to Omarchy today as a webapp/AUR package independently of this project).

### Qt-based renderer

Rejected: licensing complexity and the same widget-tree ownership problem as GTK4.

## Consequences

Positive:

- Full ownership of the frame loop makes the 120 fps target an engineering task, not a negotiation with a toolkit.
- RN-core-on-Linux is validated at retail scale (Vega), and RN-through-Skia-on-Linux reached a working proof of concept (react-native-skia); the combination is proven feasible, though no production system is publicly confirmed to pair them.
- Renderer-first scope gives a demonstrable vertical slice early: window → View → the six components → 120 fps proof.

Negative / accepted risks:

- **Text, IME, and accessibility are the graveyard of GPU-canvas toolkits.** IME (text-input protocol) and AT-SPI accessibility must be platform features, not afterthoughts; they are explicitly on the roadmap and explicitly hard.
- No native-widget fidelity: we draw everything, so we style everything.
- Ecosystem gap: TurboModule codegen/autolinking for a new platform is significant plumbing before third-party libraries work.
- Competing with an active community project splits attention; mitigated by genuinely different architecture and stated non-rivalry.

## Roadmap (milestones, each demonstrable)

- **M0** — Wayland window + Vulkan Skia surface + Hermes running a bundle; a `View` renders.
- **M1** — The six core components; Yoga layout; pointer + keyboard events.
- **M2** — Native-driven animations, damage tracking, `wp_presentation`-measured 120 fps demo on Hyprland; Tracy instrumentation.
- **M3** — TurboModule codegen + autolinking; CLI/Metro platform registration (`--platform linux`).
- **M4** — Suuudokuuu running as the flagship app; packaging story (AUR, omarchy-pkgs).
- **M5** — IME and AT-SPI accessibility groundwork.

## Notes

A detailed prior-art and effort-calibration survey (rn-skia, Vega, harmony, RNW core internals, per-tier effort estimates) is in progress and will be committed as `docs/research/prior-art.md`; its findings amend this ADR if they contradict it.
