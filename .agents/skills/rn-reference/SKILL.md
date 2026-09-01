---
name: rn-reference
description: How to mine react-native-windows, react-native-macos, react-native-harmony, and microsoft/react-native-skia for platform plumbing before designing anything from scratch. Use before implementing Fabric hosting, codegen, TurboModules, CLI/Metro registration, threading, or any component's prop surface.
---

# Mining the reference platforms

Never clone the full repos into this workspace; they are huge. Use `gh api` content reads and targeted `gh search code` queries, or shallow sparse checkouts into `/tmp`.

## Where the answers live

- **Fabric host + composition renderer:** `microsoft/react-native-windows` under `vnext/Microsoft.ReactNative/Fabric/` — ComponentView hierarchy, mounting transaction handling, `updateProps`/`updateLayoutMetrics` contracts, event emitters. This is the closest structural model to our render-tree layer (theirs composes via Windows Composition; ours draws via Skia — the Fabric-facing surface is the same).
- **Out-of-tree platform + codegen registration:** react-native-windows `vnext/` package.json, `react-native.config.js`, and their `@react-native-windows/codegen` usage; react-native-harmony's platform registration for a second worked example.
- **Desktop interaction semantics:** `microsoft/react-native-macos` for pointer hover, keyboard focus/tab order, text selection, and menu conventions — desktop behaviors mobile RN never defined.
- **Skia-on-RN precedent:** `react-native-skia/react-native-skia` (Kudo Chien / NAGRA, dormant since 2023, RN 0.71-era) for RN-core-to-Skia rendering shape and its limitations; treat as prior art, not gospel.
- **Portable C++ host:** `react/react-native` `packages/react-native/ReactCxxPlatform/` — Meta's platform-neutral ReactHost (Metro connection, Fast Refresh, inspector, timers, threading, core modules) and the `IMountingManager` seam (`executeMount`, `dispatchCommand`); plus `private/react-native-fantom/tester/CMakeLists.txt`, Meta's own desktop-Linux CMake build of the whole core. Start here before writing any host plumbing.
- **Upstream contracts:** `facebook/react-native` `packages/react-native/ReactCommon/react/renderer/` — ShadowNode, LayoutMetrics, mounting; and `packages/react-native/ReactCommon/react/renderer/animations/` for native-driven animation plumbing.

## Method

1. State the question precisely (e.g., "how does RNW map ScrollView momentum events to Fabric?").
2. `gh search code --repo microsoft/react-native-windows "<symbol or prop>"` to locate files, then `gh api repos/{owner}/{repo}/contents/{path}` for the specific files only.
3. Record what you adopted and what you diverged from (and why) in the PR description — divergence from all references needs a stated reason.
4. Check the RN version the reference targets; RNW/macos lag upstream. Prefer upstream `ReactCommon` contracts over reference-repo copies when they disagree.
