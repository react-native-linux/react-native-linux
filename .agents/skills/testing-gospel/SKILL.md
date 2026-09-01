---
name: testing-gospel
description: The react-native-linux test harness architecture and the coverage contract. Use when adding any component, renderer feature, module, or when touching CI, coverage gates, golden images, the headless compositor rig, or the e2e driver.
---

# Testing gospel

Coverage is a merge gate, not a metric: 100% line+branch on `packages/core` C++ (llvm-cov) and 100% on all TypeScript (Vitest V8 provider). Exclusions exist only as annotated, reviewed markers with reasons.

## The four layers

1. **Unit (C++/GoogleTest via CTest):** pure logic — render-tree diffing, damage-region math, prop parsing, layout adapters, animation curves. Deterministic, no GPU, no compositor. Sanitizer jobs (ASan+UBSan, TSan) run this suite on every PR.
2. **Unit (TypeScript/Vitest):** CLI, Metro platform resolution, codegen wrappers, JS-side module implementations. Colocated `*.spec.ts`.
3. **Golden-image (renderer correctness):** fixtures rendered via **lavapipe** (Mesa software Vulkan, `VK_ICD_FILENAMES` pinned) under a **headless Wayland compositor** (wlroots headless backend or `weston --backend=headless`), compared to checked-in PNGs with a perceptual diff (SSIM/ΔE threshold, not byte equality — font rasterization varies). Goldens regenerate only through the dedicated command; regenerated goldens appear in the PR diff and get reviewed like code.
4. **E2E (test-harness app):** boots a real Hermes bundle in the real platform under the headless compositor; injects input via virtual Wayland protocols (`zwlr_virtual_pointer_v1`, `virtual-keyboard-v1`); asserts on screenshots, serialized event traces, and `wp_presentation` frame-timing feedback. Perf scenarios assert p95 frame time against the 8.33 ms budget with a stated regression threshold; results are uploaded as CI artifacts for trend tracking.

## Rules

- Every component or renderer feature PR ships all applicable layers together: unit + golden + e2e. No follow-up-test PRs.
- Flaky tests are bugs: quarantine requires an issue link and a 7-day expiry.
- Frame-timing assertions never use wall-clock sleeps; they consume `wp_presentation` feedback events.
- The harness itself is covered: diff algorithm, input injector, and trace serializer have their own unit tests.
- CI must stay runnable locally: one command per layer, documented in the harness README, containerized deps (Mesa/lavapipe, weston) pinned by digest.
