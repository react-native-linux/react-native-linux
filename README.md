# react-native-linux

React Native for desktop Linux, GPU-first: Fabric rendered through Skia/Vulkan straight into a Wayland surface, Hermes for JavaScript, New Architecture only. Built for honest 120 fps on modern compositors (Hyprland first).

> Status: founding. The architecture is decided ([ADR-0001](docs/adr/0001-gpu-first-out-of-tree-react-native-platform-for-linux.md)); code lands milestone by milestone.

## Why another attempt

Linux is the only major desktop without a maintained React Native platform. The core's portability is proven — Amazon Vega ships RN on a Linux-based OS, Microsoft's react-native-skia rendered RN through Skia on Linux, react-native-harmony proved out-of-tree New Architecture platforms. Nobody has assembled those pieces for desktop Linux with a renderer that owns its frame loop.

This project draws the entire UI on a GPU canvas with a retained, damage-tracked scene — no GTK/Qt widget tree — so frame pacing, animation threading, and the 8.33 ms budget are ours to engineer. The complementary [lucid-softworks/react-native-linux](https://github.com/lucid-softworks/react-native-linux) explores the GTK4-widgets path; different architecture, shared goal.

## Architecture in one paragraph

Fabric commits diff into a persistent render tree; the render thread draws damage regions via Skia Graphite/Vulkan, paced by `wl_surface` frame callbacks and `wp_presentation` timestamps. Yoga lays out on the commit thread, Hermes runs app code on the JS thread, and animations are native-driven so a JS stall never drops a frame. Text goes through SkParagraph (HarfBuzz + FreeType) with a GPU glyph atlas.

## Packages

Published under the `@react-native-linux/*` npm scope. (The unscoped `react-native-linux` npm name is an unrelated squatted placeholder.)

## Roadmap

See the milestones in [ADR-0001](docs/adr/0001-gpu-first-out-of-tree-react-native-platform-for-linux.md#roadmap-milestones-each-demonstrable): window → six core components → measured 120 fps → codegen/autolinking → flagship app ([Suuudokuuu](https://www.suuudokuuu.com)) → IME/accessibility.

## License

[MIT](LICENSE)
