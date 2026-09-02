# C++ toolchain

`packages/core` builds `hello_react`: a bridgeless `ReactInstance` running a JavaScript bundle on Hermes, linked
against the React Native renderer core and Yoga. It is the toolchain proof for issue #3, the engine-embedding proof
for issue #9, and the headless half of the Fabric bootstrap for issue #10. It is not a renderer: nothing here draws.

`packages/core` also builds `rnl_window`: an xdg-shell window on Wayland, a FIFO Vulkan swapchain, and a Skia Ganesh
`GrDirectContext` on that device, driven by `wl_surface` frame callbacks. That is issue #8. With
`--fabric <bundle>` it boots the same `ReactInstance` and `FabricHost` inside the window process and draws the
retained scene every frame — issue #32, the first React-driven pixels. Without the flag it draws the static
placeholder card and runs no JavaScript.

`rnl_window --screenshot <output.png> [--frames N]` is the same window, run for `N` presented frames and then
read back: the last presented swapchain image is copied into a host buffer and written as a PNG, and the process
exits. That is issue #33, and with a headless compositor and lavapipe it is the only rig that proves
`SkiaVulkanRenderer`. See *Window goldens*.

`hello_react --golden <bundle> <output.png>` is the third mode and the producer half of the golden-image rig from
issue #6: it boots the same headless Fabric host, renders the settled scene into an **offscreen raster**
`SkSurface` and writes a PNG. No GPU, no Vulkan driver, no Wayland compositor. See *Golden images*.

`hello_react --inject-pointer <bundle> <x> <y>` is the fourth, and the proof for issue #18: it boots the same
headless Fabric host, synthesises a mouse sweep and a click at those coordinates through the same input pipeline
the window uses, and lets the bundle report what reached JavaScript. See *Input*.

`rnl_window --ime-debug` is issue #26's proof and needs a compositor, not a bundle: it enables `zwp_text_input_v3`
on the window as soon as it has focus and prints every composition batch an input method sends, so typing CJK with
fcitx5 running prints the pre-edit and the commit. See *IME*.

Two halves are shared rather than duplicated: `rnl_react_core` is a static library carrying the instance, the
Fabric host and the retained scene, and `rnl_scene_painter` is a static library carrying `paintScene`, the single
implementation that turns a `SceneSnapshot` into draw calls. `rnl_window` calls it once per frame into a
swapchain-backed surface; `hello_react --golden` calls it once into a raster surface. A golden produced by a second
painter would prove nothing about the window, so there is only one.

## Scope

`hello_react` boots the upstream bridgeless stack: a `JSRuntimeFactory` that wraps `makeHermesRuntime` in
`JSIRuntimeHolder`, `ReactCxxPlatform`'s `MessageQueueThreadImpl` as the JS thread, a `PlatformTimerRegistry`
implementation on a `TaskDispatchThread` feeding `TimerManager`, and `JsErrorHandler::OnJsError` printing structured
errors to stderr. `console` and `nativeLoggingHook` are installed in C++ and write to stdout and stderr. The
executable takes an optional bundle path and falls back to an inline smoke line without one.

`react/runtime/hermes` (`bridgelesshermes`) is deliberately not used: it needs `hermes_executor_common` and
`hermes_inspector_modern`, and the debugger is off in this build. The consequence is that `JSRuntime` falls back to
`FallbackRuntimeTargetDelegate`, so no CDP-backed console or sampling profiler. TurboModules, the nativemodule tree,
and the remaining ten `ReactCxxPlatform` subdirectories are still out.

## Fabric bootstrap

`hello_react --fabric <bundle>` adds a headless Fabric host on top of the same `ReactInstance`:

- `FabricHost` builds a `SchedulerToolbox` (context container carrying the `RuntimeScheduler`, buffered runtime
  executor, unbuffered bindings executor, a `FrameEventBeat`, and a component registry with the `RootView`,
  `View`, `Image`, `ScrollView`, `Paragraph`, `Text` and `RawText` descriptors), constructs a `Scheduler`, and starts one
  `SurfaceHandler` with an 800x600 layout constraint. Constructing the `Scheduler` is what installs
  `nativeFabricUIManager` into the runtime, and constructing the `Paragraph` descriptor is what constructs the
  `TextLayoutManager`; see *Text*. Constructing the `Image` descriptor is what constructs the `ImageManager`;
  see *Image*.
- The surface is started with an **empty module name**, so `SurfaceHandler::start` goes through
  `UIManager::startEmptySurface` instead of `AppRegistryBinding::startSurface`. That is the whole reason a bundle
  without React Native's JavaScript runtime can drive the renderer. `SurfaceHandler::stop` has no such branch and
  always calls `RN$stopSurface`, so `FabricHost` installs a no-op for it.
- `SchedulerDelegateImpl` from `ReactCxxPlatform` pulls each `MountingTransaction` and hands it to
  `LinuxMountingManager`, our `IMountingManager`, which applies the five `ShadowViewMutation` operations to
  `RetainedScene`. The scene is mutated in place across commits; Fabric owns the diff, we do not.
- The surface root is created by the host, not by a mutation: the differ only emits an `Update` for the root
  shadow node when its `ShadowView` changed, and on the first commit it has not.

`packages/core/test-bundles/fabric-view.js` calls `createNode`, `appendChild`, `createChildSet`,
`appendChildToSet` and `completeRoot` directly — the same `nativeFabricUIManager` surface React's Fabric
reconciler uses. Its outer `<View>` carries no paint props and is flattened away by Fabric before mounting; the
inner one carries `backgroundColor` and reaches the mounting layer. Expected output:

```text
fabric-view: committed surface 1
RootView #1 frame=(0.00, 0.00, 800.00, 600.00)
  View #4 frame=(24.00, 24.00, 120.00, 80.00) backgroundColor=rgba(51, 102, 204, 1)
```

The `(24.00, 24.00)` origin is the flattened parent's padding folded into the child's frame by the differ, and the
frame itself is Yoga output. The dump is ordered by mount order under sorted root tags, and the same scene is what
`--golden` rasterises; see *Golden images*.

Nothing in this bootstrap is headless-only. `hello_react` and `rnl_window` construct the same `ReactHost` and the
same `FabricHost`; the headless host dumps the scene once the JavaScript thread goes quiet, and the window host
draws it every frame. See *The retained scene, and the threads it crosses*.

Not covered yet, each with an owning milestone: `Scheduler::reportMount` and mount-hook telemetry,
`dispatchCommand`, multiple surfaces, and every component past `View`, `Paragraph`, `Image` and `ScrollView` —
`TextInput` in particular. Events are covered; see *Input*. Scrolling is covered; see *ScrollView*.

## Window host

`rnl_window` is five files under `packages/core/src`, in the order the frame flows through them:

- `WaylandWindow` owns one connection, one `wl_surface`, one `xdg_toplevel` titled `react-native-linux` at 800x600,
  and the run loop. It never attaches a buffer; `vkQueuePresentKHR` does that. `xdg_toplevel.close` sets the exit
  flag, `xdg_toplevel.configure` records a resize, and `xdg_wm_base.ping` is answered.
- `WaylandSeat` owns the `wl_seat` the window binds, and turns pointer and keyboard events into a queue of
  platform-neutral ones. See *Input*.
- `SkiaVulkanRenderer` owns the `VkInstance` (`VK_KHR_surface` + `VK_KHR_wayland_surface`), the device, the FIFO
  swapchain, and the `GrDirectContext`.
- `WindowSession` is the React half, and only exists with `--fabric`: it owns a `ReactHost` and a `FabricHost`
  sized by the window, loads the bundle, and hands the frame thread a scene snapshot per frame.
- `WindowMain` parses the flag, owns the run loop, and paints. Without a bundle it clears to `#14161A` and draws
  one rounded rectangle in `#3366CC`, inset 64 px with a 24 px corner radius. With one it calls `paintScene` from
  `ScenePainter`, which clears to the same background and fills one `SkRect` per painted scene node. That function
  lives outside the window because `hello_react --golden` calls it too; see *Golden images*.

### Swapchain to SkSurface

Each swapchain `VkImage` is wrapped directly as an `SkSurface` through `GrBackendRenderTargets::MakeVk` plus
`SkSurfaces::WrapBackendRenderTarget`. There is no offscreen surface and no blit, because the wrap path is both the
simpler one and the one Skia's own `tools/window/VulkanWindowContext.cpp` uses, and it leaves every image barrier to
Skia. The only layout handling in our code is the `skgpu::MutableTextureState` requesting
`VK_IMAGE_LAYOUT_PRESENT_SRC_KHR`, passed to `GrDirectContext::flush`.

Per frame: create a semaphore, `vkAcquireNextImageKHR`, `SkSurface::wait` on that semaphore (Skia takes ownership
and destroys it), paint, flush with one signal semaphore and the present state, `submit`, then `vkQueuePresentKHR`
waiting on the signal semaphore. Render semaphores are per backbuffer and there is one more backbuffer than there
are swapchain images, so a command buffer retires before its semaphore is reused. That structure is copied from
Skia's reference implementation rather than invented.

`VK_ERROR_OUT_OF_DATE_KHR` from either acquire or present rebuilds the swapchain; `xdg_toplevel.configure` rebuilds
it too, so a resize is handled from whichever side notices first.

### Pacing

ADR-0001 decision 3 is implemented literally. `wl_surface.frame` is what throttles: `requestFrameCallback` runs
immediately before `vkQueuePresentKHR`, because present is what commits the surface and `wl_surface.frame` applies
to the next commit on the connection. The run loop then blocks in `poll` on the Wayland file descriptor until the
callback arrives.

The poll timeout is the mandatory fallback, not a second frame source. Hyprland sends frame callbacks only for the
active and active-special workspaces, so a window on another workspace receives none at all and would otherwise
stop dispatching entirely — including the close event. The fallback is 50 ms, which is slow enough not to compete
with a 60 or 120 Hz callback stream and fast enough that a hidden window still notices it was closed.

Issue #8's acceptance criteria say "no timer-based render loop exists anywhere in the frame path", which predates
the ADR amendment that made the fallback mandatory. The ADR wins; the issue text needs the correction.

### The retained scene, and the threads it crosses

`rnl_window --fabric <bundle>` runs the whole stack in one process: `WindowSession` constructs a `ReactHost`
(bridgeless instance, JavaScript thread, timers, error handler) and a `FabricHost` sized by the window, then loads
the bundle. `BundleRunner` uses the same `ReactHost`; it differs only in the policy layered on top — it runs to
quiescence and exits, which a window cannot do, so the session variant exists to keep the JavaScript thread alive
for the life of the window and nothing else.

The handoff is ADR-0001 decision 6 applied literally. JavaScript runs on the instance's JavaScript thread; a commit
driven from JavaScript mounts there, when the `RuntimeScheduler` drains its rendering update. The window loop is the
platform-owned frame thread, and it calls exactly two things on the session:

- `takeFrame`, once per frame, which copies the scene out under `LinuxMountingManager`'s mutex **and** takes the
  damage accumulated since the last frame in the same lock. The copy carries
  absolute frames — parent-relative Fabric frames are accumulated during the walk, in the scene, not in the paint
  loop — plus the absolute transform, the inherited `overflow: hidden` clips, the resolved border metrics, and
  packed ARGB colours with the inherited opacity already multiplied in. See *View props fidelity*. Nodes with
  neither a visible background nor a visible border, including the surface root, contribute nothing, because the
  background clear already covers them. The pair is atomic on purpose: a transaction landing between a snapshot
  and a damage take would hand the frame thread damage its scene cannot satisfy, and that region would be painted
  from stale state and never repainted. See *Damage tracking*.
- `resize`, on `xdg_toplevel.configure`, which calls `SurfaceHandler::constraintLayout` with the new size as both
  the minimum and the maximum. That commit uses the default commit options, whose `mountSynchronously` is true, so
  the Yoga relayout **and** the resulting mount run on the frame thread. Thread affinity is therefore not what keeps
  the scene consistent — the mutex is, and it is the only thing that is.

A snapshot is still a full copy — at this scene size that is cheaper than any alternative — but a frame is no
longer a full repaint. See *Damage tracking*.

Shutdown is stop, drain, destroy, in that order: `~WindowSession` stops the surface, drains the JavaScript thread so
the queued unmount runs while the scheduler delegate is still alive, and then lets the Fabric host and the instance
destruct. `xdg_toplevel.close` and a `wl_display` error both leave the run loop normally, so both take that path.
`Ctrl-C` does not: there is still no signal handler, so `SIGINT` terminates the process where it stands and the
compositor reclaims the surface when the connection's file descriptor closes.

Not drawn yet, each with an owning milestone: shadows and elevation, `boxShadow`, `filter`, `mixBlendMode`,
`outline`, `backgroundImage` gradients, and `display: none`. Backgrounds, per-corner radii, per-side borders,
opacity, transforms and `overflow` clipping are drawn; see *View props fidelity*. Text is drawn; see *Text*.
Images are drawn; see *Image*. Pointer and keyboard events reach JavaScript once per frame; see *Input*.

### Skia acquisition

Skia publishes no official binaries and recommends tracking tip-of-tree, so the choice was a maintained third-party
prebuilt or a `gn`/`ninja` source build. M0 takes the prebuilt, and specifically **rust-skia's**:

| Candidate | Verdict |
| --- | --- |
| `aseprite/skia` `m151` | Rejected. Its shipped `out/Release-x64/args.gn` sets no `skia_use_vulkan`, and `gn/skia.gni` defaults it to false, so Ganesh is GL-only. Vulkan headers ship, the implementation does not. |
| `JetBrains/skia` `m152` (skiko) | Rejected. Vulkan is on, but the Linux build is gcc-10 with `-D_GLIBCXX_USE_CXX11_ABI=0 -fno-exceptions -fno-rtti`. The old libstdc++ string ABI is viral across the whole link, and this build already links ReactCommon and Hermes with the modern ABI. `JetBrains/skia-pack` is archived and its Linux build had no Vulkan either. |
| `rust-skia/skia-binaries` `0.153.2` | **Chosen.** Vulkan and Ganesh both on, modern cxx11 ABI, clang-20 against libstdc++, system freetype and fontconfig, and a `textlayout` variant that already carries SkParagraph for M1. |
| Source build at a `chrome/mNNN` branch | Deferred. It is the documented follow-up, not the M0 path. |

The pin is split because the rust-skia release ships archives without headers:

| Component | Pin |
| --- | --- |
| Skia milestone | `m153` |
| Static libraries | `skia-binaries-b0260d93e48425b4b39f-x86_64-unknown-linux-gnu-ganesh-gl-jpegd-jpege-pdf-textlayout-vulkan.tar.gz` from release `0.153.2`, sha256 `918fa4ff44211ea0df405ae1e86cb1fbcd5593a6500f67b12a378690d8b49668` |
| Headers | `rust-skia/skia` at `c9c3c5a91e9d74181a2ca34de81d78fef9b4d2b6`, sparse `include` and the three module `include` trees |

Both live in `scripts/skia.lock.json` and are fetched by `scripts/vendor-skia.ts` into `third_party/skia`, which
verifies the sha256 before extracting and writes a stamp so a re-run is a no-op. Re-pinning is an edit to the lock
file followed by `pnpm --filter @react-native-linux/core vendor:skia`.

**Tracked risk.** ADR-0001 already records that Skia has no ABI and no releases; this adds a second-hand build to
that. The archive is produced by a Rust binding project for its own consumption, on its own schedule, from a fork
of `google/skia`; `0.153.2` is a release of the binaries repository, and the corresponding crate version was still
mid-flight when it was pinned. Nothing about it is a contract. The sha256 makes the artifact immutable for us, and
the documented exit is a source build at a `chrome/mNNN` branch with `skia_use_vulkan=true`, which is the same
recipe the AUR `skia-static` package uses. That exit should be taken before Skia lands in CI, because CI cannot
depend on a third party's release cadence.

## Pins

| Component | Pin | Source of truth |
| --- | --- | --- |
| React Native | `v0.87.1` | `scripts/vendor.lock.json` |
| Hermes | `hermes-v250829098.0.17` | `third_party/react-native/packages/react-native/sdks/hermes-engine/version.properties`, read at configure time |
| folly | `v2024.11.18.00` | `RNL_FOLLY_VERSION` in `packages/core/CMakeLists.txt`, mirroring RN's `gradle/libs.versions.toml` |
| fast_float | `v8.0.0` | `RNL_FAST_FLOAT_VERSION`, same mirror |
| Boost (fallback only) | `1.83.0` | `RNL_BOOST_VERSION`, same mirror; used only when no system Boost is found |
| glog (fallback only) | `v0.7.1` | `RNL_GLOG_VERSION`; used only when no system glog is found |
| Skia | `m153` prebuilt | `scripts/skia.lock.json` |
| Noto Sans | `notofonts.github.io@2ad4e55`, per-file sha256 | `scripts/fonts.lock.json` |
| xdg-shell | system `wayland-protocols` | `pkg-config --variable=pkgdatadir wayland-protocols` |

The Hermes tag is derived, never hardcoded: `hermes-v${HERMES_VERSION_NAME}`. `sdks/.hermesversion` was removed
upstream and must not be used. The Hermes CMake library target is `hermesvm`, not `libhermes`.

Re-vendoring on a React Native bump: edit `tag` in `scripts/vendor.lock.json`, run `pnpm --filter @react-native-linux/core vendor`,
reconfigure. The script re-clones when the pin changes and is a no-op when it has not.

## Prerequisites

Boost and glog are looked up with `find_package` first and fetched from source only when the lookup fails, so a
machine without root can build without them. `double-conversion`, `fmt` and ICU have no fallback and must be present.

### Arch Linux

```bash
sudo pacman -S --needed base-devel git cmake ninja ccache python \
  boost boost-libs double-conversion fmt google-glog icu
```

`rnl_window` needs, on top of that:

```bash
sudo pacman -S --needed wayland wayland-protocols libxkbcommon vulkan-headers vulkan-icd-loader \
  freetype2 fontconfig
```

`libxkbcommon` is what turns the keymap the compositor sends into keysyms; `RNL_ENABLE_WINDOW` probes for it and
disables `rnl_window` without it. See *Input*. `wayland-protocols` carries two XML files this build generates
from — `stable/xdg-shell/xdg-shell.xml` and `unstable/text-input/text-input-unstable-v3.xml` — and both are probed
for by path; see *IME*.

Typing CJK into the window needs an input method, which is a developer-machine dependency rather than a build one
and is never installed in CI: `sudo pacman -S --needed fcitx5 fcitx5-configtool fcitx5-chinese-addons`. See the
checklist in *IME*.

`hello_react --golden` needs only `freetype2` and `fontconfig` from that list, plus the vendored Skia archive and,
for anything with text in it, the vendored fonts. It never opens a Wayland connection and never loads a Vulkan
driver.

The window golden rig needs two more, and only for that rig:

```bash
sudo pacman -S --needed weston vulkan-swrast
```

`weston` is the headless compositor and `vulkan-swrast` is lavapipe, whose ICD manifest the rig finds at
`/usr/share/vulkan/icd.d/lvp_icd.x86_64.json`. Note that lavapipe is **not** in the `mesa` package on Arch; a
machine with `mesa` and a hardware driver still has no `lvp_icd` manifest and the rig will say so and skip. See
*Window goldens*.

`wayland-scanner` ships inside the `wayland` package and is located through
`pkg-config --variable=wayland_scanner wayland-scanner`, so it is never assumed to be on `PATH`. A Vulkan driver
package is separate from the loader: `vulkan-radeon`, `vulkan-intel`, `nvidia-utils`, or `vulkan-swrast` for
lavapipe.

`clang` and `llvm` are optional but recommended: Meta's own Linux C++ host (`react-native-fantom`) builds with
`CC=clang`, and Hermes' Linux CI does the same, so clang is the better-tested path for the Hermes and llvh sources.

### Without root

`cmake` and `ninja` can come from a user-space version manager; prefix every CMake invocation, for example
`mise exec cmake@latest ninja@latest -- cmake --preset dev`. Boost and glog are then fetched from source. The Boost
fallback downloads the classic 145 MB source archive into the preset's `build/<preset>/_deps`, once per build
directory, so installing system Boost is still the faster path when it is available.

### Ubuntu (CI)

This is the list `.github/workflows/ci.yml` installs, and the only Ubuntu configuration this project claims to
support:

```bash
sudo apt-get install -y build-essential git cmake ninja-build ccache python3 pkg-config \
  clang-18 clang-tools-18 libclang-rt-18-dev llvm-18 \
  libboost-dev libdouble-conversion-dev libfmt-dev libgoogle-glog-dev libicu-dev \
  libfreetype-dev libfontconfig-dev
```

`pkg-config`, `libfreetype-dev` and `libfontconfig-dev` are what `RNL_ENABLE_SKIA` probes for; without them
configure disables the painter and `hello_react --golden`, and the golden-image rig has nothing to run.

Version caveats on Ubuntu 24.04, all understood and accepted:

- `libfmt-dev` is 10.x while React Native pins fmt 12.1.0. folly's subset only needs fmt >= 8.
- `cmake` is 3.28.3, exactly the minimum this build declares.
- `libboost-dev` is 1.83.0, exactly `RNL_BOOST_VERSION`, and it ships
  `/usr/lib/x86_64-linux-gnu/cmake/Boost-1.83.0/BoostConfig.cmake`, so `find_package(Boost CONFIG)` resolves
  `Boost::headers` and the 145 MB source fallback never runs. The two paths deliver the same Boost, so installing
  it is a pure saving rather than a divergence.
- `libgoogle-glog-dev` is 0.6.0, not the 0.7.1 the fallback builds. It ships its own CMake package files defining
  `glog::glog`, and it pulls `libgflags-dev` and `libunwind-dev`, which its config module looks for. Only the
  logging macros ReactCommon uses are involved, and those are unchanged between 0.6 and 0.7.

`fast_float` has no Ubuntu package, so it is fetched at React Native's pinned version rather than split across
two acquisition paths.

The compiler is `clang-18`, Ubuntu 24.04's default clang, and not the clang 22 the fix ledgers below were written
against. Newer clang is stricter than older clang, not laxer, so the ledger entries are a superset of what 18
needs; the libstdc++ include-hygiene workarounds are keyed to the standard library, which is libstdc++ 13 here.
Two packages exist only because Debian splits them out of `clang`: `libclang-rt-18-dev` carries the ASan, UBSan
and TSan runtimes, without which the sanitizer presets fail at link, and `clang-tools-18` carries
`clang-scan-deps`, which CMake demands if any subproject resets `CMP0155` to `OLD` and re-enables C++20 module
scanning. If clang 18 ever proves to be the problem, the documented escape is to pin a specific `apt.llvm.org`
toolchain rather than to float on whatever `clang` resolves to.

gcc remains the documented-but-untested alternative. CI builds one compiler, clang, because it is the one the
local development machine, Hermes' own Linux CI, and Meta's `react-native-fantom` host all use; a gcc column
would double the wall-clock cost of the matrix to test a path nothing else exercises.

## Commands

```bash
pnpm --filter @react-native-linux/core vendor        # node scripts/vendor-react-native.ts
pnpm --filter @react-native-linux/core vendor:skia   # node scripts/vendor-skia.ts
pnpm --filter @react-native-linux/core vendor:fonts  # node scripts/vendor-fonts.ts — see *Text*
pnpm --filter @react-native-linux/core configure    # cmake -S <repo root> --preset dev
pnpm --filter @react-native-linux/core build        # cmake --build build/dev
pnpm --filter @react-native-linux/core run:hello    # build/dev/bin/hello_react
pnpm --filter @react-native-linux/core run:fabric   # build/dev/bin/hello_react --fabric <bundle>
pnpm --filter @react-native-linux/core run:golden   # hello_react --golden <bundle> /tmp/rnl-fabric-view.png
pnpm --filter @react-native-linux/core run:golden:damage  # hello_react --damage-golden damage.js /tmp/rnl-damage.png
pnpm --filter @react-native-linux/core run:golden:text    # hello_react --golden text.js /tmp/rnl-text.png
pnpm --filter @react-native-linux/core run:golden:image   # hello_react --golden image.js /tmp/rnl-image.png
pnpm --filter @react-native-linux/core run:golden:scroll  # hello_react --scroll-to scroll.js /tmp/rnl-scroll.png 160 100 3
pnpm --filter @react-native-linux/core run:golden:focus   # hello_react --focus-tab focus.js /tmp/rnl-focus.png 3
pnpm --filter @react-native-linux/core assets:test-image  # node scripts/make-test-image.ts — see *Image*
pnpm --filter @react-native-linux/core run:input    # hello_react --inject-pointer pressable.js 200 140
pnpm --filter @react-native-linux/core run:window   # build/dev/bin/rnl_window
pnpm --filter @react-native-linux/core run:window:fabric  # build/dev/bin/rnl_window --fabric <bundle>
pnpm --filter @react-native-linux/core run:window:ime      # build/dev/bin/rnl_window --ime-debug — see *IME*

pnpm test:golden          # compare renders against the checked-in goldens
pnpm test:golden:update   # regenerate the goldens, for review before committing
pnpm test:golden:window          # the same, for the window goldens: weston + lavapipe + the real swapchain
pnpm test:golden:window:update   # regenerate the window goldens
pnpm test:golden:window:render   # run the rig alone, writing PNG files into build/window-goldens
pnpm test:native          # configure/build the `test` preset, run ctest, gate on coverage — see *Unit tests and coverage*
```

The same sequence without pnpm, from the repository root:

```bash
node scripts/vendor-react-native.ts
node scripts/vendor-skia.ts
cmake --preset dev
cmake --build build/dev
./build/dev/bin/hello_react
./build/dev/bin/hello_react packages/core/test-bundles/hello.js
./build/dev/bin/hello_react --fabric packages/core/test-bundles/fabric-view.js
./build/dev/bin/hello_react --inject-pointer packages/core/test-bundles/pressable.js 200 140
./build/dev/bin/hello_react --focus-tab packages/core/test-bundles/focus.js /tmp/rnl-focus.png 3
./build/dev/bin/rnl_window
./build/dev/bin/rnl_window --fabric packages/core/test-bundles/fabric-view.js
./build/dev/bin/rnl_window --ime-debug
```

Without an argument the expected output is `react-native-linux: hermes alive`. With
`packages/core/test-bundles/hello.js` the bundle prints its own evaluation, microtask and timer lines and the
process exits 0. `packages/core/test-bundles/throws.js` is the error fixture: it prints a `[js-error] fatal` block
with a parsed stack to stderr and exits 1. `--fabric` takes the bundle path as its own argument and prints the
retained scene after the JavaScript thread goes quiet; see *Fabric bootstrap* above. `--golden` takes the bundle
path and an output path, prints nothing of its own, and writes a PNG. `--damage-golden` takes the same arguments,
runs a bundle that commits twice, and writes a PNG only if the damage-clipped redraw of the second commit is
byte-identical to a full one; see *Golden images*. On a build configured
without Skia it exits 1 with a message naming `scripts/vendor-skia.ts`. `--scroll-to` takes the bundle path, an
output path, a surface coordinate and a wheel notch count, turns the wheel over that point, lets the momentum
settle, and writes the PNG of where it stopped; see *ScrollView*. `--focus-tab` takes the bundle path, an output
path and a press count, presses Tab that many times and writes the PNG of where the focus ring landed, printing
whatever the bundle prints on the way; see *Focus and keyboard*. `--inject-pointer` takes the bundle path
and a surface coordinate, clicks there, and prints whatever the bundle prints; see *Input*. `--ime-debug` takes no
value, composes with every other `rnl_window` flag, and prints composition events; see *IME*.

When `cmake` and `ninja` are not on `PATH`, wrap the two CMake steps:

```bash
mise exec cmake@latest ninja@latest -- cmake --preset dev
mise exec cmake@latest ninja@latest -- cmake --build build/dev
```

Presets: `dev` (Debug), `release` (Release), `asan` (ASan + UBSan), `tsan` (TSan). The sanitizer presets also flip
Hermes' own `HERMES_ENABLE_*_SANITIZER` options, because Hermes disables its Boost.Context fibers under ASan and
cannot infer that from `CMAKE_CXX_FLAGS`. `CMAKE_EXPORT_COMPILE_COMMANDS` is on in every preset, so clangd works
from a fresh configure.

### Building and running rnl_window

```bash
node scripts/vendor-skia.ts
mise exec cmake@latest ninja@latest -- cmake --preset dev
mise exec cmake@latest ninja@latest -- cmake --build build/dev --target rnl_window
./build/dev/bin/rnl_window
```

Two options gate the drawing targets, both defaulting to `ON`:

- `RNL_ENABLE_SKIA` covers everything that draws. When Skia, freetype, fontconfig or `pkg-config` is absent,
  configure prints `Skia is unavailable, missing: ...` and skips `rnl_scene_painter`, `rnl_window` and
  `hello_react --golden`. `hello_react` itself still builds and still runs bundles.
- `RNL_ENABLE_WINDOW` covers only the Wayland half. When Skia is present but `wayland-scanner`, `wayland-client`,
  the xdg-shell protocol or the Vulkan loader is not, configure prints `rnl_window is disabled, missing: ...` and
  skips the target rather than failing.

Both skip rather than fail, so the headless build survives on a machine that has none of it.
`cmake --preset dev -DRNL_ENABLE_WINDOW=OFF` skips the window deliberately and silently, and
`-DRNL_ENABLE_SKIA=OFF` skips everything that draws.

The xdg-shell client header and private code are generated at build time by `wayland-scanner` into
`build/<preset>/packages/core/wayland-protocols` and compiled into `rnl_xdg_shell`. The same happens for
`text-input-unstable-v3` into `rnl_text_input`; that one comes from `unstable/` rather than `stable/`, and
`RNL_ENABLE_WINDOW` probes for both XML files separately so a missing one names itself. Nothing generated is
checked in. See *IME*.

### Window test checklist

Run these under a real Hyprland session, from the repository root. The expected picture is a dark `#14161A`
background filling the window with one blue `#3366CC` rounded rectangle inset 64 px on every side.

1. `./build/dev/bin/rnl_window` opens a window titled `react-native-linux` at 800x600 with the picture above, and
   prints nothing.
2. Drag a corner to resize. The rectangle keeps its 64 px inset on all four sides at every size, with no stretching,
   no tearing, and no black band at the edge while dragging. Resize to a very small window and back.
3. Close the window with the compositor's close binding (`SUPER Q` on stock Omarchy). The process exits 0 —
   `echo $?` — and no `wl_display` protocol error is printed.
4. `Ctrl-C` in the terminal also terminates it. There is no signal handler, so this is a plain `SIGINT` kill and the
   compositor should not leave a stale window behind.
5. Move the window to another workspace, wait ten seconds, come back. It must still be alive and repainting. This is
   the frame-callback fallback: Hyprland stops sending frame callbacks off the active workspace, so without the poll
   timeout the process would be stuck in `poll` forever.
6. With the window on another workspace, close it from the compositor. It must still exit, on the same path.
7. `WAYLAND_DISPLAY= ./build/dev/bin/rnl_window` prints
   `[rnl-window] wl_display_connect failed; is WAYLAND_DISPLAY set?` and exits 1.
8. Optional, for a driver-independent check:
   `VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/lvp_icd.x86_64.json ./build/dev/bin/rnl_window` renders the same
   picture through lavapipe. That is the driver the window goldens use; the raster golden rig renders offscreen on
   the CPU and never reaches one. See *Window goldens*.
9. `./build/dev/bin/rnl_window --fabric packages/core/test-bundles/fabric-view.js` opens the same window on the
   same dark background with one flat blue `#3366CC` rectangle, 120x80, its top-left corner at (24, 24) — the
   frame `hello_react --fabric` prints for `View #4`, drawn instead of dumped. The bundle's
   `fabric-view: committed surface 1` line goes to stdout as usual.
10. Resize that window. The rectangle keeps its size and its (24, 24) origin, because the bundle uses fixed sizes
    and a padded root; the point of the check is that the surface relayouts without crashing. Close it, and the
    process exits 0 — the surface is stopped and the JavaScript thread drained before anything is destroyed.
11. `./build/dev/bin/rnl_window --fabric` with no path prints
    `[rnl-window] --fabric requires a bundle path` and exits 1. A path that does not exist exits 1 through the
    same `[rnl-window]` handler.
12. `./build/dev/bin/hello_react --golden packages/core/test-bundles/fabric-view.js /tmp/fabric-view.png` writes
    that same picture as an 800x600 PNG without opening a window, without a Vulkan driver and with
    `WAYLAND_DISPLAY` unset. That is the golden-image rig; see *Golden images*.
13. `./build/dev/bin/rnl_window --fabric packages/core/test-bundles/damage.js` shows a blue tile, a green tile and
    a red tile, and one second later the green tile has moved down and right and turned amber while the red one is
    gone. **No stale rectangle is left behind at either old position**, which is what partial redraw gets wrong
    when it gets anything wrong; and both before and after that second, the window is idle and drawing nothing.
    Resize it while it is idle — the picture must come back intact, because a new swapchain full-damages every
    image. See *Damage tracking*.

14. `./build/dev/bin/rnl_window --fabric packages/core/test-bundles/text.js` shows the text golden's picture in a
    real window: a bold heading, a wrapping paragraph with coloured fragments, a one-line paragraph ending in an
    ellipsis, centred and right-aligned lines, a letter-spaced line and an underlined one. Resize it — the text
    must not reflow, because every element is absolutely positioned and fixed-width, which is what makes the
    picture comparable to the golden. Run
    `pnpm --filter @react-native-linux/core vendor:fonts` first; without it the paragraphs are shaped with a
    system font and the window will not match the golden. See *Text*.

15. `./build/dev/bin/rnl_window --fabric packages/core/test-bundles/image.js` shows the image golden's picture in
    a real window: twelve tiles, five of them one `resizeMode` each. The point of running it in a window rather
    than headless is the **damage** path — the tiles appear a frame or two after the window opens, because the
    decodes finish after the first commit, and every one of them has to appear. A tile that stays empty panel
    until the window is resized means the decode-completion damage did not reach the frame thread. The
    `https://` tile at (182, 360) is expected to stay empty panel forever. See *Image*.

16. `./build/dev/bin/rnl_window --fabric packages/core/test-bundles/scroll.js` shows a 200x150 dark panel at
    (60, 60) with a red band at its top, and a blue marker rectangle at (60, 240) that must never change. Turn the
    wheel over the panel: the coloured rows move, they are cut cleanly at the panel's top and bottom edges, and
    **nothing paints outside the panel** — a row appearing beside or on the marker is a broken clip. Let the wheel
    go and the rows keep gliding and slow to a stop rather than halting with the notch. Turn it up past the top or
    down past the bottom and the content stops dead at the edge, with no bounce, which is the deferred rubber band.
    Two-finger scroll on a touchpad tracks the fingers one-to-one and flings when they lift. See *ScrollView*.

Not covered by this checklist, because it is not implemented yet: fractional scale, pointer and keyboard input,
`wp_presentation_feedback` timing, and any measurement of the frame budget. Composition has a checklist of its own,
because it needs an input method running; see *IME*.

## Damage tracking

ADR-0001 decision 2 asks for per-frame cost proportional to change rather than to scene size. This is issue #12,
and it is three pieces: the scene accounts for what changed, the renderer decides what a given swapchain image
still owes, and `paintScene` turns that into a clip. Nothing about the scene model changed to make room for it.

### What a mutation damages

The rule is uniform, and it is the whole of the accounting: **every mutation damages the extent of the affected
subtree as it was before the mutation and as it is after it.** A create has no before, a delete has no after, and
everything else has both. `createSurfaceRoot` is the one special case: it damages the whole surface, because a new
or resized surface has no relationship at all to the pixels that were there.

| Mutation | Damage |
| --- | --- |
| `Create` | The new extent. Fabric emits `Create` before `Insert`, so this measures the node where it stands, parentless — one redundant rectangle per created node, which the cap below absorbs. |
| `Insert` | The extent before the insert and after it, which is what makes a reparent damage both places. |
| `Remove` | The same pair. A removed node is not a deleted node: it becomes its own root and keeps painting at its own frame origin until the `Delete` arrives. |
| `Delete` | The old extent. |
| `Update` | The old and the new extent, which covers a move, a resize, a colour change, an opacity change and a transform change without distinguishing between them. |
| `createSurfaceRoot` | The whole root frame. |

A **subtree extent** is the union of the bounds of every primitive the subtree paints, where one primitive's bounds
are its absolute frame's four corners mapped through its absolute matrix and bounded, then intersected with every
`overflow: hidden` clip it inherited — each clip's own frame mapped through the clip's own matrix. Three
consequences are worth stating because they are the reason the rule stays this short:

- **Borders need no term.** React Native draws them inside the frame, so the frame already contains them.
- **A parent's transform, opacity or clip change damages its whole subtree**, because the extent is defined over
  the subtree rather than over the node. That is the correct answer and it is also the cheap one; per-descendant
  precision would cost an analysis nobody has asked for yet.
- **A primitive that its clips cut away entirely damages nothing**, and a subtree that paints nothing — a layout
  container with no background, an `opacity: 0` subtree — has no extent and damages nothing.

The extent is computed by running the ordinary snapshot walk over that one subtree, seeded with the paint state its
ancestors produce. There is no second implementation of frame composition, transform composition or clip
inheritance for damage to get subtly wrong; that is the point.

A node whose parent chain is broken — an insert under a tag that was never created — is measured as if it were a
root. Nothing paints it, so the damage is a region that did not need repainting. A superset is safe; the inverse
is not.

### The merge policy

Damage is a list of absolute-space rectangles, capped at **eight**. The ninth rectangle collapses the entire list
into its bounding rectangle, and accumulation continues from there. Rectangles are never merged pairwise and
overlapping rectangles are kept: an overlap costs the intersection being painted twice, and a merge heuristic costs
code that would need its own tests. A mutation batch large enough to blow the cap is a batch whose bounding
rectangle is most of the surface anyway.

`mergeDamage` applies the same policy when one damage list is folded into another, which is what the renderer does
per swapchain image.

### Swapchain images are not one buffer

`vkAcquireNextImageKHR` returns whichever image is free. The pixels already in it are **not** last frame's — they
are from the last frame that used *that* image, two or three frames ago. Clipping to the damage since the previous
frame would leave every image carrying the changes it personally missed.

`SkiaVulkanRenderer` therefore keeps **one damage list per swapchain image**, adds every frame's damage to all of
them, hands the acquired image its own accumulated list, and clears that list once the image is painted. That is
the buffer-age approach — the region an image owes is everything that changed since it was last drawn — without
needing `VK_EXT_swapchain_maintenance1`'s age query, because we know when we drew each image. The alternative,
falling back to a full redraw whenever the acquired image is not the most recent one, was rejected: with three
images it would make two frames out of three full redraws, which is most of the win.

Two properties fall out of it rather than being special-cased:

- **A new swapchain is a full repaint.** `createBackbuffers` seeds every list with the whole surface, so startup
  and every resize repaint everything, for every image, before any partial redraw happens.
- **An idle frame costs nothing, and it bypasses Skia.** An empty list means that image already holds the current
  scene, so `drawFrame` skips the paint entirely. The first version of this also flushed Skia anyway, on the
  assumption that `GrDirectContext::flush` with a signal semaphore submits a command buffer even when no drawing
  was recorded. **That assumption was wrong, and it deadlocked the window on hardware** — see item 8 of the window
  fix ledger. Skia's own header says so: flush returns `GrSemaphoresSubmitted::kNo` when it submits no semaphores,
  and in that case "the client should not have the GPU wait on any of the semaphores passed in with the
  `GrFlushInfo`", which is exactly what `vkQueuePresentKHR` does with the render semaphore. An idle frame is
  therefore a submission of its own: one `vkQueueSubmit` with **zero command buffers**, waiting on the acquire
  semaphore at `VK_PIPELINE_STAGE_ALL_COMMANDS_BIT` and signalling the render semaphore. That is the whole of a
  frame that draws nothing, and it needs no layout handling because nothing touched the image since the flush that
  left it in `VK_IMAGE_LAYOUT_PRESENT_SRC_KHR`.

  Ownership of the acquire semaphore moves with the branch. On the drawing path `SkSurface::wait` takes it and
  Skia deletes it; on the idle path Skia never sees it, so it is parked on the backbuffer and destroyed the next
  time that backbuffer comes round. That is the same retirement guarantee the extra backbuffer already provides
  for reusing the render semaphore, and it is what keeps an idle window from leaking one semaphore per frame.

  A pending `--screenshot` capture is the one thing that overrides the idle path: the frame is painted even when
  the image owes nothing, so the screenshot always comes from a full repaint on the ordinary path and the readback
  is ordered after real submitted work. See *Window goldens*.

Not done, and deferred to the perf issue #20: `wl_surface.damage_buffer` and `VK_KHR_incremental_present`. Both
tell the *compositor* which part of the surface changed, so it can composite less. Everything above only stops
*us* from drawing; the compositor still treats every commit as a full-surface update. They are additive to this
design — the per-image list is already exactly the rectangle set both APIs want — and neither is a correctness
requirement.

### Where the clip happens

`paintScene` takes the damage and, when it is non-empty, clips to the union of its rectangles before it clears.
Each rectangle is rounded out to whole pixels and outset by one, and the clip is not anti-aliased: a partially
covered clip edge would blend new drawing into whatever the previous frame left there, and a partial pixel is
exactly what a full repaint would not produce. `SkCanvas::clear` is `SkBlendMode::kSrc` and respects the clip, so
the damaged region is replaced rather than blended and everything outside it is left byte-for-byte as it was.

An empty damage list means no clip at all — the full repaint the golden rig and the first frame of a surface want.

Primitives outside the damage are still submitted to Skia and clipped there rather than culled in our loop.
Culling them is a CPU-side saving that belongs with the rest of the frame-time work in #20; the correctness
argument does not need it.

## View props fidelity

ADR-0001's paint-model ownership means React Native's `<View>` styling semantics are implemented on the canvas
rather than negotiated with a widget toolkit. This is what issue #13 implemented, and what it deliberately did not.

`RetainedScene` reads the props and does all the composition; `ScenePainter` only issues Skia calls. That split is
not stylistic: `RetainedScene.cpp` is inside the 100% line-and-branch coverage gate and `ScenePainter.cpp` is not,
so every number that can be wrong is computed where a unit test can see it.

| Prop | How it is implemented |
| --- | --- |
| `backgroundColor` | Filled as the outer rounded rect. Skipped when `isColorMeaningful` is false, as before. |
| `opacity` | Multiplied down the tree during the snapshot walk and folded into the alpha channel of every colour the node emits. |
| `borderRadius` and the per-corner props | `BaseViewProps::resolveBorderMetrics` — the same call iOS and Android make — cascades the corners, resolves percentages against the frame, and applies the CSS corner-overlap clamp. The painter turns the result into an `SkRRect` with per-corner x/y radii. |
| `borderWidth` and the per-side props | Read from the Yoga style through `resolveBorderMetrics`, drawn **inside** the frame as the ring between the outer rounded rect and the inset inner one (`drawDRRect`). |
| `borderColor` and the per-side props | One `drawDRRect` when the four colours are equal; otherwise one wedge clip per side, so each side keeps its own colour and each corner is split on the diagonal. |
| `overflow: hidden` | `getClipsContentToBounds` pushes the node's rounded border box onto a clip stack that descendant primitives carry; the painter replays it with `clipRRect` under the clipping ancestor's own transform. |
| `transform` | `BaseViewProps::resolveTransform` resolves the operation list and folds in `transformOrigin`; the scene applies it about the centre of the absolute frame, composes it with every ancestor transform, and reduces the 4x4 to a 2D affine the painter hands to `SkMatrix`. |
| `zIndex` | Nothing to implement. Fabric sets `ShadowNode::orderIndex_` from `zIndex` in `ConcreteViewShadowNode` and stable-sorts siblings by it in `sliceChildShadowNodeViewPairs` before diffing, so mount order — which is the scene's child order and therefore paint order — is already `zIndex` order. |

Known deviations from iOS and Android, all deliberate:

- **Opacity is per-primitive, not group opacity.** React Native composites a translucent subtree as one layer; we
  multiply the alpha of each primitive instead. Overlapping descendants of a translucent ancestor therefore blend
  against each other where the real thing would not. Fixing it means `saveLayerAlphaf` per stacking context.
- **`borderStyle` is ignored.** Every border is solid; `dotted` and `dashed` are not drawn differently.
- **`borderCurve` is ignored.** Corners are always circular, never iOS' `continuous` squircle.
- **An unset `borderColor` draws nothing.** The edge is skipped when the resolved colour is not meaningful, which
  keeps borders consistent with the background path; iOS and Android fall back to black.
- **`overflow: hidden` clips to the border box**, matching iOS `clipsToBounds`, where CSS clips to the padding box.
- **Transforms are 2D only.** The 4x4 is reduced to its affine part, so `perspective`, `rotateX` and `rotateY` lose
  their depth and collapse to their in-plane component, and `backfaceVisibility` is not honoured.
- **Anti-aliased wedge clips can leave a hairline seam** where two border sides of different colours meet.

Not implemented at all, and owned elsewhere: `shadowColor`/elevation and `boxShadow`, `filter`, `mixBlendMode` and
`isolation`, `outline*`, and `backgroundImage` gradients. Each needs its own issue under M1; none of them is a
variation on what is here.

## Text

ADR-0001 decision 7 chose SkParagraph. This is issue #14, and it is four pieces: a `TextLayoutManager` that
answers Yoga, a font strategy that makes the answer reproducible, the paragraph inputs travelling through the
scene, and the painter drawing the same paragraph that was measured.

### The pipeline, end to end

```text
<Text>/<RawText> shadow nodes
  → ParagraphShadowNode::getContent          flattens them into one AttributedString
  → TextLayoutManager::measure               SkParagraph layout → Size → Yoga
  → ParagraphState                           the AttributedString + ParagraphAttributes, mounted
  → RetainedScene::writeNode                 read off the state into SceneNode::text
  → SceneSnapshot                            SceneTextContent on the primitive, opacity folded in
  → ScenePainter::paintText                  the same layoutParagraph call, painted at the frame origin
```

`<Text>` and `<RawText>` never reach the mounting layer: they are not view-forming, and `ParagraphShadowNode`
flattens the whole subtree into a single `AttributedString`. `<Paragraph>` is the only text component the scene
ever sees, and `ParagraphState` is the only place its content exists there. All three descriptors are registered
in `FabricHost` anyway, because the reconciler cannot create a node for a component with no descriptor.

There is exactly one function that turns an `AttributedString` into a laid-out `skia::textlayout::Paragraph`,
`layoutParagraph` in `src/TextPipeline.cpp`, and both the measurement and the paint call it with the same two
values. That is deliberate and it is the whole reason the scene carries React Native's own types rather than a
resolved copy of them: a second description of the same text is a second chance for the drawn line breaks to
disagree with the measured ones, and a paragraph that wraps differently than it was measured is the classic text
bug in a GPU renderer.

Shaping is HarfBuzz, compiled into `libskshaper.a`. Segmentation — grapheme, word and line breaks — is ICU 78,
compiled with its data into `libskunicode_icu.a`. Rasterisation is FreeType. None of them is a system dependency:
only freetype and fontconfig are linked from the machine, which `RNL_ENABLE_SKIA` already probed for.

### Replacing the upstream stub

`react/renderer/textlayoutmanager/platform/cxx/.../TextLayoutManager.cpp` is a stub whose `measure` returns
`layoutConstraints.minimumSize` and shapes nothing; `docs/research/prior-art.md` §2.4 names it as one of the two
real C++ gaps. Replacing it is replacing one translation unit, and the mechanism is a **source-list edit at our
own `add_subdirectory` call site**, in `packages/core/CMakeLists.txt`:

```cmake
get_target_property(RNL_TEXT_LAYOUT_MANAGER_SOURCES react_renderer_textlayoutmanager SOURCES)
list(REMOVE_ITEM RNL_TEXT_LAYOUT_MANAGER_SOURCES ${RNL_TEXT_LAYOUT_MANAGER_STUB})
list(APPEND RNL_TEXT_LAYOUT_MANAGER_SOURCES .../src/TextLayoutManager.cpp .../src/TextPipeline.cpp)
set_property(TARGET react_renderer_textlayoutmanager PROPERTY SOURCES ${RNL_TEXT_LAYOUT_MANAGER_SOURCES})
```

Four properties of that choice are load-bearing:

- **No vendored file is edited.** The vendored `CMakeLists.txt` globs its own platform directory; we change what
  the resulting target compiles, from outside it, after `add_subdirectory` returns. A re-vendor is still a clean
  checkout. The configure fails loudly with a named error if the stub ever moves, rather than silently keeping it.
- **Our sources join that object library rather than `rnl_scene_painter`,** because the references to
  `TextLayoutManager::measure` are in that library's own objects and a static archive only resolves backwards.
- **The declaration stays the vendored `platform/cxx` header,** which fixes the shape of the class. `measure` is
  the only method there, so the C++20 concepts in `TextLayoutManagerExtended` report `measureLines` and
  `prepareLayout` unsupported and `ParagraphShadowNode` takes its non-prepared path. See *Fidelity limits*.
- **It is conditional on Skia.** `-DRNL_ENABLE_SKIA=OFF` keeps the upstream stub, so the two sanitizer presets and
  the Hermes-free `test` configure link no Skia at all and are byte-for-byte the builds they were before. The cost
  is that text measures as zero under those presets, which is the pre-existing behaviour rather than a new bug.

### Font strategy, and why goldens need it

Pixel-exact goldens and system font resolution are incompatible: Arch and `ubuntu-24.04` do not ship the same
default sans-serif, so the same bundle would produce two different golden images. The `FontCollection` therefore has two
managers, and the order matters:

| Manager | What it is | Reproducible |
| --- | --- | --- |
| Asset | `SkFontMgr_New_Custom_Directory` over `packages/core/fonts` | Yes — pinned files, verified by sha256 |
| Default | `SkFontMgr_New_FontConfig` | No — whatever the machine has installed |

Skia consults the asset manager first, so the default family resolves to the vendored file everywhere. Fontconfig
remains behind it, which is what satisfies issue #14's "fonts resolved through fontconfig" for a bundle that asks
for a family by name, and what supplies glyph fallback for codepoints the bundled font does not cover.

**Goldens must stay inside the vendored font's coverage.** Anything that falls through to fontconfig — another
family, an emoji, a script Noto Sans does not carry — is not reproducible and must not go into a checked-in PNG.
That is the honest reason `text.js` is ASCII, and the reason issue #14's RTL and emoji goldens are deferred rather
than approximated.

The font is **Noto Sans**, hinted static Regular, Bold and Italic, under the **SIL Open Font License 1.1**, pinned
by commit and sha256 in `scripts/fonts.lock.json` and fetched by `scripts/vendor-fonts.ts` into
`packages/core/fonts`, which is git-ignored exactly like `third_party`. The licence text is fetched alongside the
faces. It is vendored rather than checked in for the same reason Skia is: the lock file is the artifact under
review, and a re-run against an unchanged lock is a no-op.

```bash
pnpm --filter @react-native-linux/core vendor:fonts
```

The directory path reaches the code as `RNL_BUNDLED_FONT_DIR`, an absolute path baked in at configure time. That
is deliberate for now — there is no asset packaging, and inventing one before the CLI exists is the kind of
scaffolding the Prime Directive rejects. Packaging fonts into an installable bundle belongs with M3.

### The cache

`TextLayoutManager` already owns `textMeasureCache_`, upstream's `TextMeasureCache`: a 1024-entry thread-safe LRU
keyed on the layout-affecting parts of the attributed string, the paragraph attributes, the layout constraints and
the pixel scale factor, with equality and hashing that deliberately ignore colour. `measure` populates it and
nothing else caches anything, because a paragraph cache keyed on the same inputs would be the same cache. Skia's
own `ParagraphCache`, inside the `FontCollection`, caches shaped runs underneath both.

What is **not** implemented is issue #14's measurable hit-rate probe. Instrumenting it belongs with the frame-time
work in #20, where there is somewhere to report a number to.

### Threading

`measure` runs on whichever thread commits, and the painter runs on the frame thread, so both can be inside
`layoutParagraph` at once. The `Paragraph` objects are per-call and never shared, but the `FontCollection` behind
them is a process-wide singleton carrying Skia's paragraph cache, and neither it nor `layout` is documented as
thread-safe. Build-and-layout therefore runs under one mutex in `TextPipeline.cpp`. Note that the TSan job
configures with `-DRNL_ENABLE_SKIA=OFF`, so TSan does not exercise this path; the mutex is reasoning, not a
measured result.

### Props implemented

| Prop | How it is implemented |
| --- | --- |
| `color` | `TextStyle::setColor`, with the inherited view opacity already multiplied into the alpha by the scene. |
| `backgroundColor` on a `<Text>` fragment | `TextStyle::setBackgroundPaint`. On the `<Paragraph>` itself it is a `<View>` background and upstream deliberately strips it from the text attributes. |
| `fontSize` | Multiplied by `fontSizeMultiplier`; unset means upstream's 14. |
| `fontWeight` | The numeric weight straight into `SkFontStyle`; synthetic bolding is Skia's business. |
| `fontStyle` | `italic` and `oblique` map to the matching `SkFontStyle::Slant`. |
| `fontFamily` | Asked for first, ahead of the bundled family and `sans-serif`, as one name rather than a CSS list. |
| `lineHeight` | Points converted to Skia's multiple-of-font-size `height`, with half leading, which is what CSS and both React Native platforms do. |
| `letterSpacing` | `TextStyle::setLetterSpacing`. |
| `textAlign` | `left`, `right`, `center`, `justify`, `start`/`end`, and `auto` as `start`. |
| `textDecorationLine` | `underline`, `line-through` and both together; `textDecorationColor` falls back to the foreground colour. |
| `numberOfLines` | `ParagraphStyle::setMaxLines`. |
| `ellipsizeMode` | Anything but `clip` sets a `…` ellipsis. |
| Inline attachments | Added as SkParagraph placeholders sized from the attachment's own measured frame, and reported back through `getRectsForPlaceholders`. |

### Fidelity limits

Each is deliberate, and each is a thing to fix rather than a thing to argue about:

- **No `measureLines`, so no baseline alignment and no `onTextLayout`.** Both go through
  `TextLayoutManagerExtended`, which detects them with a concept on the class the vendored header declares. Adding
  them means adding methods to a vendored header, which is a different decision from swapping one source file.
- **No prepared-layout path.** `prepareLayout`/`measurePreparedLayout` are absent for the same reason, so every
  measure lays out from scratch behind the measure cache.
- **RTL is not handled.** The paragraph direction is hardcoded left-to-right. ICU is present and SkParagraph does
  the bidi work, so mixed-direction runs inside a paragraph resolve correctly; what is missing is `writingDirection`
  and an RTL base direction, and there is no golden for either.
- **Ellipsis is always at the tail.** SkParagraph truncates nowhere else, so `head` and `middle` are accepted and
  drawn as `tail`.
- **Emoji are whatever fontconfig finds.** Skia's COLRv1 support exists, but the vendored font carries no emoji,
  so every emoji is a fallback lookup and therefore machine-dependent. Not goldenable as things stand.
- **`adjustsFontSizeToFit`, `textTransform`, `fontVariant`, text shadows, `textAlignVertical` and
  `textBreakStrategy` are ignored.**
- **Group opacity applies to text the same way it applies to views**: per-fragment alpha, not a composited layer.
  Overlapping translucent text blends against itself. Same deviation, same fix, as *View props fidelity*.
- **Every paint rebuilds the paragraph, and every snapshot copies the attributed string.** The damage walk runs
  the same snapshot code over a subtree per mutation, so a text-heavy tree copies its strings more than it needs
  to. Skia's shaped-run cache absorbs the layout half. Both are #20 concerns, not correctness ones.
- **`<TextInput>` is not here at all.** It has no `platform/cxx` upstream and is issue #17; selection, the caret,
  and `zwp_text_input_v3` IME all belong to it.

## Image

Issue #15, milestone M1. This is four pieces: an `ImageManager` that answers `ImageShadowNode`, a decode pipeline
that never runs on a frame thread, a bounded cache the painter reads, and a damage path for the one event in this
renderer that no Fabric mutation stands behind — a decode finishing.

### The pipeline, end to end

```text
<Image> props
  → ImageShadowNode::getImageSource        picks the source and stamps the laid-out size onto it
  → ImageManager::requestImage             queues the decode, returns an ImageRequest
  → ImageState                             the chosen ImageSource + that request, mounted
  → RetainedScene::writeNode               read off the state and the props into SceneNode::image
  → SceneSnapshot                          SceneImageContent on the primitive, opacity folded in
  → ScenePainter::paintImage               the cached SkImage, fitted and clipped to the frame

  decode worker thread → ImageCache → decode listener → LinuxMountingManager::damageImageSource
```

The pixels never travel through the scene. `RetainedScene` links no Skia, so a `ScenePrimitive` carries the source
URI, the fit and the tint, and the decoded `SkImage` lives in a process-wide cache the painter looks the URI up
in. That is the same split *Text* makes for a different reason, and it is what keeps the whole of the image model
inside the coverage gate.

The source is read off `ImageState`, not off `ImageProps.sources`. `ImageShadowNode` is what chooses between
several sources and what stamps the laid-out size and scale onto the one it chose, and that is the source
`requestImage` was given and therefore the source the decoder is filling the cache with; reading the props would
be a second answer to the same question. `resizeMode` and `tintColor` come off the props, because neither reaches
the state.

### Replacing the upstream stub

`react/renderer/imagemanager/platform/cxx/.../ImageManager.cpp` is marked `// Not implemented` and returns a
request nothing ever completes — the second of the two real C++ gaps `docs/research/prior-art.md` §2.4 names.
Replacing it is the same **source-list edit at our own `add_subdirectory` call site** the `TextLayoutManager` swap
uses, in `packages/core/CMakeLists.txt`, against `react_renderer_imagemanager` instead. `ImageContent.cpp`,
`ImageManager.cpp` and `ImagePipeline.cpp` join that object library; no vendored file is edited; the configure
fails loudly with a named error if the stub ever moves; and `-DRNL_ENABLE_SKIA=OFF` keeps the stub, so the
sanitizer presets and the Hermes-free `test` configure link no Skia and an `<Image>` there simply never paints.

Replacing the definition rather than subclassing `ImageManager` is deliberate, and it is why nothing inserts an
`"ImageManager"` entry into the context container. `ImageComponentDescriptor` resolves its manager with
`getManagerByName<ImageManager>(contextContainer, "ImageManager")`, which **constructs**
`std::make_shared<ImageManager>(contextContainer)` when that key is absent. Neither shipping platform registers
under it either; iOS and Android inject their loaders under their own keys from inside the manager. Replacing the
definition therefore gives every descriptor this implementation with no registration step and no second
implementation to disagree with.

### Threading, and what the frame thread is promised

`requestImage` runs during layout, on whichever thread commits. It queues the URI and returns immediately; one
process-wide worker thread does every decode. A frame is therefore never blocked on a codec, which is the whole
acceptance criterion, and the painter draws whatever is in the cache at the moment it asks — a source that has
not finished decoding draws nothing at all rather than stalling the frame.

Two requests for the same URI decode once: the second joins the first's completion list. The queue, the cache,
the completion lists and the listener live under one mutex, and that mutex is never held while a codec runs or
while a completion or the listener is called, so the listener may take the mounting manager's lock without
inverting a lock order.

`ImagePipelineState` is a function-local static exactly like the text pipeline's `FontCollection`, and its
destructor stops and joins the worker before any member the worker touches is destroyed. That is what makes a
function-local static safe to hand a thread.

### Damage, because a decode is not a mutation

Every other thing that changes the picture arrives as a `ShadowViewMutation`, and *Damage tracking* accounts for
it. A decode does not: no shadow node changed, so Fabric emits nothing. The pipeline therefore calls one listener
with the URI it just decoded, `FabricHost` installs a listener that calls
`LinuxMountingManager::damageImageSource`, and that damages the subtree extent of every node drawing that URI
under the scene mutex. The next `takeFrame` hands the frame thread that damage with the scene it belongs to,
which is the same atomic pair every other mutation goes through.

The listener is process-wide and installed by `FabricHost`, under `RNL_ENABLE_IMAGES` — the definition only
exists when the swap above happened. It is cleared in `~FabricHost` so a decode that lands after a host is gone
damages nothing.

`hello_react --golden` has no run loop to notice that damage, so it settles the decode queue with
`waitForPendingImageDecodes` before it rasterises. Without that the same bundle would produce a picture that
depends on how fast a codec ran.

### The cache

`ImageCache` is a least-recently-used cache keyed by source URI, bounded at **64 MiB of decoded pixels**. The
bound is bytes rather than entries because the cost of a decoded image is its pixels and two sources can differ by
three orders of magnitude in area. An insert past the capacity evicts from the least recently used end until the
total fits, and an entry that alone exceeds the capacity is not cached at all, so one oversized image cannot flush
everything else out on its way to being evicted itself. Both a hit and an insert make an entry most recently used.

The value is a `std::shared_ptr<void>`, which is what keeps `ImageContent.cpp` free of Skia and therefore inside
the coverage gate; it is the same type erasure upstream's own `ImageResponse` uses, for the same reason. An
evicted image stays alive as long as a frame is still drawing it, because the painter takes its own reference.

### Sources

| Source | Handled how |
| --- | --- |
| `data:<type>;base64,<payload>` | Decoded in `ImageContent.cpp`, then handed to Skia as an `SkData`. Only base64 is accepted, which is the only form React Native's tooling emits. |
| `file:///absolute/path` | The scheme is stripped and the rest is a path. |
| `/absolute/path` | A path already. |
| `relative/path.png` | Resolved against `RNL_BUNDLED_ASSET_DIR`, an absolute path baked in at configure time pointing at `packages/core/assets`. |
| `http://`, `https://`, any other scheme | Unsupported. One line on stderr, no decode, nothing painted. |

`RNL_BUNDLED_ASSET_DIR` is the same placeholder `RNL_BUNDLED_FONT_DIR` is, for the same reason: there is no asset
packaging, and inventing one before the CLI exists is the kind of scaffolding the Prime Directive rejects. Real
asset packaging belongs with M3.

Decoding is Skia's own codecs, compiled into `libskia.a`: `SkPngDecoder` and `SkJpegDecoder` are passed to
`SkCodec::MakeFromData` as an explicit decoder list rather than relying on `SkCodecs::Register`, so the supported
set is visible in the source instead of depending on which objects the linker happened to pull in. libpng, zlib
and libjpeg-turbo are inside the archive, so images add no system dependency — the pinned release's name carries
`jpegd` and `jpege`.

### Props implemented

| Prop | How it is implemented |
| --- | --- |
| `source` | The first — or best-fitting — entry of `ImageProps.sources`, chosen by `ImageShadowNode` and read back off `ImageState`. |
| `resizeMode` | `cover`, `contain`, `stretch`, `center` and `repeat` are `imagePlacement` in `ImageContent.cpp`: a destination rectangle from the frame and the decoded size. `none` maps onto `center`, because both draw at the natural size. |
| `tintColor` | `SkColorFilters::Blend` with `SkBlendMode::kSrcIn`, so the image becomes a silhouette in that colour. The inherited opacity is folded into the tint's own alpha. |
| `opacity` and every `<View>` paint prop | `ImageProps` derives from `ViewProps`, so backgrounds, borders, radii, transforms and clips are exactly *View props fidelity*. The image is drawn after the background and before the border, because React Native draws borders inside the frame and therefore over the content. |
| `borderRadius` | The image is clipped to the same rounded rectangle the background is filled with, so `cover` overflow and rounded corners are cut by one clip. |

`repeat` is the one mode that is not a single `drawImageRect`: the placement rectangle is the first tile, and a
repeating shader anchored at it fills the frame.

### Fidelity limits

Each is deliberate, and each is a thing to fix rather than a thing to argue about:

- **No `http` or `https`.** There is no networking stack in this build at all — `ReactCxxPlatform`'s HTTP client
  needs nlohmann_json and OpenSSL, and neither is linked. A remote source is not a decode that failed; it is a
  decode that was never attempted. It arrives with the Metro dev server work.
- **No animated images.** `SkCodec` is asked for one frame. APNG, animated WebP and GIF decode to their first
  frame at best; GIF is not in the decoder list at all.
- **No `srcSet` or scale selection.** `ImageShadowNode` picks the best area fit among several sources and we paint
  what it picked, but nothing here reasons about `scale` or a device pixel ratio, because there is no fractional
  scale support yet either.
- **No `onLoad`, `onLoadStart`, `onLoadEnd`, `onError` or `onProgress`.** The `ImageResponseObserverCoordinator`
  upstream builds for every request **is** completed or failed by the decoder, so the state is truthful and these
  events are a matter of emitting them from an observer rather than of plumbing. Nothing observes one yet.
- **The image fills the border box, not the padding box.** iOS and Android inset the content by padding; here
  `padding` on an `<Image>` moves nothing.
- **`blurRadius`, `capInsets`, `overlayColor`, `fadeDuration`, `progressiveRenderingEnabled`, `defaultSource` and
  `loadingIndicatorSource` are ignored.** `capInsets` in particular means no nine-patch stretching.
- **A failed decode is not retried and not remembered.** The next commit that changes the source requests it
  again; nothing polls.
- **The cache has no hit-rate probe**, for the same reason the text measure cache has none: there is nowhere to
  report a number to until #20.

## ScrollView

Issue #16, milestone M1. ADR-0001 already records this one as an accepted risk — *"ScrollView physics fidelity is
hand-written and will feel wrong for a long time"* — because React Native's scrolling semantics are `UIScrollView`
semantics and a GPU canvas gets nothing free from a scrolled-window widget. This is the first slice of it: the two
axes, the clip, wheel and touchpad input, the deceleration curve, and `onScroll`. The deferrals are at the end and
each names what it would take.

Upstream owns more of this than any other component so far. `ScrollViewShadowNode` lays children out relative to
the ScrollView and never moves them; the scroll position lives in `ScrollViewState::contentOffset`, which
`getContentOriginOffset` already subtracts for hit testing and view culling; and `ScrollViewEventEmitter` already
has every event. **There is no cross-platform scrolling**: every platform is expected to move that number itself
and emit those events from it, which is exactly what `RCTScrollViewComponentView` does on iOS from
`scrollViewDidScroll`. So the platform contribution is three pieces and nothing else.

### The pipeline, end to end

```text
wl_pointer.axis / axis_discrete / axis_stop
  → WaylandSeat                    queued raw, with the last pointer position attached
  → InputQueue                     deltas summed per axis; the axis event behind a notch dropped
  → FabricHost::dispatchInput      the frame is split: scroll here, pointer there
  → ScrollController::route        findNodeAtPoint, then the deepest ScrollView above the hit node
  → ScrollController::advance      one frame of ScrollPhysics per axis, per ScrollView
      ├─ ConcreteState::updateState        contentOffset back into the shadow tree
      └─ ScrollViewEventEmitter            onScroll, onMomentumScrollBegin, onMomentumScrollEnd
  → FabricHost::induceEventBeat    both of those flush on the JavaScript thread together
  → commit → mount → RetainedScene::writeNode      contentOffset read back off ScrollViewState
  → SceneSnapshot                  children composed from the frame origin minus contentOffset, clipped
```

### The physics, and what the constants mean

`ScrollPhysics.{h,cpp}` is pure arithmetic over doubles — no React types, no Skia, no threads — which is what puts
it inside the coverage gate. Everything that can be numerically wrong lives there.

| Constant | Value | Where it comes from |
| --- | --- | --- |
| `kDecelerationRateNormal` | `0.998` | `UIScrollViewDecelerationRateNormal`. React Native's `decelerationRate` prop resolves `"normal"` to it and `BaseScrollViewProps::decelerationRate` carries it unchanged, so the prop is read rather than remapped. |
| `kDecelerationRateFast` | `0.99` | `UIScrollViewDecelerationRateFast`, the same way. |
| `kWheelNotchDistance` | `40.0` points | Ours. A notch has no distance on iOS because iOS has no wheel. |
| `kMinimumMomentumTravel` | `0.5` points | Ours. When less than half a point of travel is left, the glide ends. |

**The rate is per millisecond, not per frame.** Momentum at `v` points per millisecond is at `v * rate^t` after `t`
milliseconds, and one frame advances by `v * (rate^dt - 1) / ln(rate)` — the exact integral over the frame rather
than a rectangle of it. Two things follow, and both are asserted in `ScrollTest.cpp` rather than claimed here:

- **The distance is frame-rate independent.** A 120 Hz window and a 60 Hz one travel the same distance for the
  same flick, and a dropped frame changes when the content arrives rather than where it stops.
- **The per-frame factors at 60 Hz are `0.998^16.667 = 0.967184` and `0.99^16.667 = 0.845771`.** Those are the
  numbers to compare against a per-frame implementation elsewhere; they are not what this code stores.

Three consequences of that shape are worth stating because they are why it is this short:

- **A wheel notch is a velocity, not a jump.** `velocityForTravel(distance, rate)` is the inverse of the momentum
  integral, so a notch injects the velocity whose curve covers exactly 40 points. A wheel and a touchpad fling
  therefore share one curve, turning the wheel again mid-glide accelerates for the same reason a second flick
  does, and *n* notches travel exactly *n* × 40 points because the curve is linear in the velocity.
- **The stop threshold costs frames, not distance.** When under half a point of travel remains, the remainder is
  applied in that same step and the velocity is zeroed, so a fling always covers the full analytic distance of its
  velocity. That is what makes a scrolled golden reproducible: the settled offset is `notches × 40` and not
  `notches × 40` minus whatever the threshold happened to swallow.
- **`decelerationRate` values the curve is undefined at are clamped, not rejected.** React Native accepts `0`
  ("stop the moment the finger lifts") and `1` ("never stop"), and both divide by `ln(rate)`. They resolve to
  `0.5` and `0.9999` per millisecond, which are the two ends of the range where the integral is finite.

A touchpad is the other input and does not go through that curve while the fingers are down: `wl_pointer.axis`
deltas move the content one-to-one, and the velocity they imply over their frame is tracked so that the
`axis_stop` that follows starts the fling from it. A frame in which the fingers were still therefore flings
nothing, which is correct, and it relies on libinput sending `axis_stop` in the same display frame as the last
delta — which it does, since it emits it immediately after the fingers lift.

`axis_source` is bound and ignored. `axis_discrete` is what a wheel sends and `axis_stop` is what a finger sends,
and those two already are the whole distinction, so consulting a third event would add a state machine and no
information. The one thing the queue does need to know is that a `wl_pointer` frame carrying a notch sends
`axis_discrete` **and** an `axis` measuring the same notch in units nobody standardises, so a continuous delta
directly behind a discrete one on the same axis is dropped as the duplicate it is.

### Routing, and why it is not the pointer's target

`ScrollController::route` hit-tests with `UIManager::findNodeAtPoint` — the same call a click uses, so a wheel and
a click agree about what is under the pointer — and then walks up: `ShadowNodeFamily::getAncestors` returns the
path from the root down as (parent, child index) pairs, and the first `ScrollViewShadowNode` found walking that
path backwards is the innermost ScrollView containing the hit node. That is nested-ScrollView behaviour with no
second rule for it, and a wheel over a node with no ScrollView above it does nothing.

`FabricHost::dispatchInput` splits the frame rather than handing every event to both routers, because the two
answer different questions about the same coordinate and a shared pass would hit-test each event twice.

### Threading, and the state write-back path

Everything the controller does runs on the platform frame thread, between `dispatchInput` and `induceEventBeat`.
That is not incidental: `ConcreteState::updateState` enqueues into the same `EventQueue` the event emitters
enqueue into, so the offset and the `onScroll` describing it flush together, in one beat, on the JavaScript thread.
Three upstream guarantees carry it:

- `findNodeAtPoint`, `getNewestCloneOfShadowNode` and `getAncestors` all take the shadow tree's shared lock and are
  documented as callable from any thread, and committed shadow nodes are immutable.
- `EventQueue::enqueueStateUpdate` **replaces** a queued update for the same family rather than appending, so a
  frame's worth of intermediate positions costs one commit no matter how many frames went by before the beat.
- `EventEmitter::dispatchUniqueEvent`, which is what `onScroll` uses, collapses repeated `scroll` events for one
  target the same way. The `onScroll` cadence is therefore **at most one per frame**, and one per beat when the
  JavaScript thread is behind.

**The platform's offset is authoritative between commits, not the mounted one.** A controller that read
`contentOffset` back off the shadow tree every frame would stall for a frame every time the JavaScript thread was
busy, because the write it made last frame may not have been applied yet. The controller therefore seeds its
position from `ScrollViewState::contentOffset` the first time a ScrollView is scrolled — so a `contentOffset` prop
and a remembered position both survive — and owns it from then on. `updateState` is called in its transforming
form rather than its replacing one, so a commit landing mid-frame keeps its own `contentBoundingRect`.

The entry for a ScrollView lives as long as the ScrollView does: `getNewestCloneOfShadowNode` returning null is an
unmounted node, and that is what drops it.

### What the scene does with it

`RetainedScene` reads `contentOffset` off `ScrollViewState` in `writeNode`, exactly as it reads text off
`ParagraphState` and an image source off `ImageState`, and the whole of scrolling in the scene is one subtraction:
a ScrollView's children are composed from its frame origin **minus** `contentOffset`. That is the same
`-contentOffset` translation `getContentOriginOffset` applies for hit testing, so the picture and the hit test
cannot disagree.

A ScrollView also clips unconditionally, which is `UIScrollView.clipsToBounds` and matches every other platform.
It reuses the existing `overflow: hidden` clip stack rather than adding a second clipping mechanism, so the painter
needs no ScrollView case at all — see *View props fidelity*.

Damage needs no new rule either. A state change is a `ShadowView` change, so Fabric emits an `Update` for the
ScrollView, and *Damage tracking*'s uniform rule already damages the subtree extent before and after — which for a
clipping node is bounded by its own frame. `RetainedSceneScrollTest` asserts that an offset change damages exactly
the ScrollView frame and nothing outside it.

### The proof

```bash
hello_react --fabric packages/core/test-bundles/scroll.js
hello_react --scroll-to packages/core/test-bundles/scroll.js /tmp/rnl-scroll.png 160 100 3
```

`packages/core/test-bundles/scroll.js` is a 200x150 `<ScrollView>` at (60, 60) holding six 200x70 rows spaced 80
points apart — 470 points of content, so 320 points of it can be scrolled — plus a blue marker rectangle at
(60, 240), directly below the viewport and outside it.

`--fabric` prints the scene, including the ScrollView's `contentOffset`, which is what proves the descriptor is
registered and the state reached the mounting layer. `--scroll-to` turns the wheel three notches over (160, 100),
integrates the physics at a fixed 60 Hz step until it comes to rest, and writes the PNG of where it stopped. The
fixed step is deliberate: a headless run has no compositor to pace it, and the settled position has to be a
property of the notch count rather than of how fast the machine looped. The beat is induced once at the end rather
than once per frame, because a state update replaces the previous one for the same node and only the last position
could survive the flush anyway.

Three notches is 120 points, so `packages/core/goldens/scroll.png` is the picture at `contentOffset = (0, 120)`:

1. The second row, green `#98C379`, **cut off at the top edge** of the viewport — 30 points of it, at the top.
2. Ten points of panel `#1E2430`, the gap between rows.
3. The third row, blue `#61AFEF`, whole.
4. Ten more points of panel.
5. The fourth row, amber `#E5C07B`, **cut off at the bottom edge** — 30 points of it.
6. The blue `#3366CC` marker at (60, 240), a clean 200x40 block. Content that leaked past the clip would land on
   or beside it, so this rectangle is the clip assertion.

The first row is above the viewport and the last two are below it; neither may appear anywhere. A golden at offset
zero would prove none of this, which is why the flag exists rather than a sixth `--golden` fixture.

`ScrollPhysics.cpp` is in the coverage gate at 100% line and branch, and `ScrollTest.cpp` is what holds it there:
the clamp, the two named rates' per-frame decay, the wheel notch travelling exactly its distance, 60 Hz and 120 Hz
agreeing, the stop threshold folding its remainder in, an edge stopping momentum dead, the degenerate rates, the
drag velocity, and the queue's scroll coalescing. `ScrollController.cpp` is deliberately outside it, and the split
is the same one *Unit tests and coverage* draws for `InputDispatcher.cpp`: what is left there is a `UIManager` hit
test, an ancestor walk and four upstream calls, all of which need a committed shadow tree. `--scroll-to` is the
test for those.

### Deferrals, with owners

- **Rubber-band overscroll.** Reaching either end stops the momentum dead. `UIScrollView` stretches past the edge
  with a logarithmic resistance curve and springs back, `bounces` and `alwaysBounce*` select it, and every one of
  those props is parsed and ignored here. It is a second curve and a second state, and it belongs with the issue
  that also gets `onScrollEndDrag`'s `targetContentOffset` right.
- **`pagingEnabled`, `snapToInterval`, `snapToOffsets`, `snapToAlignment`, `disableIntervalMomentum`.** All parsed,
  none implemented. Snapping is a projection of the momentum's landing point onto a grid, which needs the
  landing-point calculation the rubber band also needs.
- **`onScrollBeginDrag` and `onScrollEndDrag`.** Only `onScroll`, `onMomentumScrollBegin` and `onMomentumScrollEnd`
  are emitted. The drag pair carries the velocity and target offset `FlatList` and paging read, so it lands with
  snapping rather than before it.
- **Scroll indicators.** `showsVerticalScrollIndicator`, `scrollIndicatorInsets`, `indicatorStyle` and
  `persistentScrollbar` draw nothing. A scrollbar is a painted overlay with its own fade timer and its own hit
  region, which is a component, not a prop.
- **`contentInset`, `contentInsetAdjustmentBehavior`, `scrollAwayPaddingTop`, `centerContent`.** The viewport is
  the ScrollView's frame and the content is `contentBoundingRect.size`; no inset is applied and the offset range is
  `[0, content - viewport]` on each axis. `ScrollEvent::contentInset` is emitted as zero.
- **`maintainVisibleContentPosition`.** Content growing above the viewport currently moves what is on screen. The
  prop is parsed and ignored; honouring it means comparing child frames across commits, which is a commit hook.
- **Programmatic scrolling.** `scrollTo`, `scrollToEnd` and `scrollResponderScrollTo` arrive as `dispatchCommand`,
  and `LinuxMountingManager::dispatchCommand` is still empty. Nothing animates to a position yet.
- **`scrollEventThrottle` is ignored.** The cadence is one `onScroll` per frame, which is the fastest React Native
  ever asks for; a throttle that fires *less* often is what `FlatList` sets, and honouring it means dropping events
  the frame already coalesced. Whether `FlatList` windowing behaves correctly at this cadence is untested — there
  is no `FlatList` here yet, because there is no React Native JavaScript runtime in this host.
- **Zoom.** `zoomScale`, `minimumZoomScale`, `maximumZoomScale` and `pinchGestureEnabled` do nothing;
  `ScrollEvent::zoomScale` is always 1. A pinch needs `wl_touch` or a gesture protocol neither of which is bound.
- **`horizontal`, `directionalLockEnabled` and RTL.** Both axes are always live and clamp independently, so a
  `horizontal` ScrollView works because its vertical axis has nothing to scroll rather than because the prop was
  read. Directional lock and a right-to-left origin are not modelled.
- **`axis_value120` and high-resolution wheels.** `wl_pointer` version 8 replaces `axis_discrete` with
  `axis_value120`, which reports fractional notches from a free-spinning wheel. The seat binds version 5, and
  raising that floor is a change to what a compositor must advertise rather than a change to this code — the
  controller already takes a fractional notch count.
- **Keyboard scrolling.** Page Up, Page Down, Home, End and arrow keys scroll nothing. That needs the focus model
  *Input* defers.
- **E2E scroll traces.** Issue #16 asks for scroll-position-over-time assertions under a headless compositor with
  virtual Wayland input. `--scroll-to` is the unit-level and integration-level proof and asserts the settled
  position; a trace over frames belongs with the same harness the lavapipe window golden runs under.

## Input

Issue #18, milestone M1. A mouse pointer and a keyboard, delivered to React once per frame. Touch, gestures beyond
press, focus traversal and IME are not here; the deferrals are at the end of this section and each names its owner.

The pipeline is four hops, and only the first and the third are ours:

```text
wl_pointer / wl_keyboard ─▶ WaylandSeat ─▶ InputQueue ─┐        frame thread
                                                       │
                              InputDispatcher ◀────────┘
                                    │ findNodeAtPoint, PointerRouter
                                    ▼
                              TouchEventEmitter ─▶ EventQueue ─▶ EventBeat
                                                                    │ induce, once per frame
─────────────────────────────────────────────────────────────────── ▼ ──────────  JavaScript thread
                              PointerEventsProcessor ─▶ UIManagerBinding ─▶ RN$ event handler
```

### The event beat, and why per-frame batching falls out of it

Upstream requires every platform to subclass `EventBeat`, because only the host knows when a frame's events are
complete: iOS induces before the main run loop sleeps, Android induces from the Choreographer. `EventQueue`
requests a beat whenever an event is enqueued, `EventBeat::induce` schedules the flush through
`RuntimeScheduler::scheduleWork`, and the queue then drains on the JavaScript thread. Until this issue, `FabricHost`
handed `SchedulerToolbox` a plain `EventBeat`, which has no way to be induced from outside — so events queued and
never left. The window was a display, not an application.

`FrameEventBeat` in `FabricHost.cpp` is the whole platform contribution: a subclass that widens the protected
`induce` to something the frame thread can call. `WindowMain` calls `WindowSession::deliverInput` once per frame,
before `takeFrame`, whether or not the compositor sent anything, and that call dispatches the frame's input and
then induces. Batching is therefore not a policy layered on top of upstream — it is what upstream already does
once the beat exists, and it applies to every queued event, not only input: a layout event and a
JavaScript-driven event ride the same flush.

`ReactCxxPlatform` ships `RunLoopObserverManager`, which reaches the same behaviour through a `RunLoopObserver`
whose `startObserving` and `stopObserving` are both empty and whose `onRender` is the induce trigger. It was read
before this was written and not adopted: there is no run loop to observe here either, so the observer buys an
indirection and no behaviour, and `RunLoopObserverManager::induce` is declared in the header with no definition in
the `.cpp`, which would be a link error the first time it was called.

Thread affinity is the base class's problem, deliberately. `induce` is called on the frame thread and the beat
callback runs on the JavaScript thread, because `scheduleWork` is what crosses over; nothing in our code touches a
`jsi::Runtime`.

### The seat

`WaylandSeat` binds `wl_seat` at version 5 and takes the pointer and the keyboard from it. Five is the floor
because `wl_pointer.frame` and the `release` requests both arrive there; a compositor that advertises less gets no
seat at all rather than a version ladder, and the window still opens with input permanently empty.

Keysyms are libxkbcommon's. The compositor sends a keymap over a file descriptor, the client `mmap`s it, compiles
it with `xkb_keymap_new_from_string`, and thereafter `xkb_state_key_get_one_sym` turns an evdev keycode — plus the
X11 keycode offset of 8 that every keymap in the wild is written against — into a keysym, and
`xkb_state_mod_name_is_active` reports Ctrl, Shift, Alt and Super after each `wl_keyboard.modifiers`. This is the
same library that `zwp_text_input_v3` compose sequences and dead keys will need in #26, so it is where the IME
work starts rather than a stopgap.

Buttons are mapped from `BTN_LEFT`/`BTN_MIDDLE`/`BTN_RIGHT` to the DOM button numbers 0, 1 and 2 at the seat, so
nothing above it knows an evdev code. Anything else the mouse has is dropped, because there is no DOM button
number for it. `wl_pointer.button` carries no coordinates, so the seat remembers the last motion position and
attaches it.

The listener structs are value-initialised and then filled member by member instead of with a designated
initialiser. `wl_pointer_listener` grows a member with every `wl_pointer` version libwayland learns — `warp`
arrived in 1.24 — and naming them all would pin this file to one libwayland release, while naming only some is
what `-Wmissing-field-initializers` exists to complain about. Everything version 5 can send is assigned; the rest
stay null and are unreachable, because the bound version is what decides which events a compositor may send.

### The queue, and what coalescing actually promises

`InputQueue` collapses **consecutive** motion events into the last one. A 1000 Hz mouse produces about seventeen
motions inside a 60 Hz frame and nine inside a 120 Hz one, and React has no use for the intermediate positions —
but it does need to know that a button went down between two of them, so a press splits the run and the positions
on either side survive independently. Contiguity is the whole rule; there is no time window and no sampling.

The queue is bounded at 256 events and counts what it drops rather than dropping silently. Coalescing is what
keeps a real device orders of magnitude below the cap, so reaching it means a stream no human produced.

`InputQueue` needs no lock. Wayland listeners run inside `wl_display_dispatch_pending`, which the frame thread
calls, and the frame thread is also what drains the queue.

### Hit testing is upstream's

`UIManager::findNodeAtPoint` — the same call `NativeDOM` uses for `elementFromPoint` — walks the committed shadow
tree from the root, honours `pointerEvents`, transforms and overflow inset, and returns the deepest node that can
be a touch target. The retained scene also carries absolute frames and could answer the same question, but it has
no shadow nodes behind those frames and therefore no event emitters, and a second hit-test implementation would be
a second chance to disagree with React about what was clicked. When nothing is hit the target is the surface root,
which is what lets the hover chain notice that the pointer left a view for the background.

Reading the shadow tree from the frame thread is allowed rather than tolerated: `ShadowTreeRegistry::visit` and
`ShadowTree::getCurrentRevision` both take a shared lock and are documented as callable from any thread, committed
shadow nodes are immutable, and `EventQueue::enqueueEvent` takes its own mutex.

### What the platform decides, and what React decides

`PointerRouter` is a mouse state machine and nothing more: which buttons are down, which node the press started
on, and therefore whether a release is also a click. Everything else a desktop pointer implies —
`pointerEnter`, `pointerLeave` between siblings, `pointerOver`, `pointerOut`, the hover chain, pointer capture and
bubbling — is computed by upstream's `PointerEventsProcessor` when the event reaches `UIManagerBinding` on the
JavaScript thread, and the platform's job is to feed it the raw events it expects.

`click` is the one exception. Upstream treats it as synthetic and passes it straight through without hover
processing or listener filtering, so deciding that a press and a release on the same target are a press gesture is
the platform's call. That is the event `Pressability` turns into `onPressIn`, `onPressOut` and `onPress`.

Keyboard has no cross-platform Fabric surface to target. On the `cxx` platform `ViewEventEmitter` is
`BaseViewEventEmitter`, which carries touch, pointer, layout, focus and accessibility events and no key events at
all — `react-native-macos` adds `onKeyDown` through a platform `HostPlatformViewEventEmitter`, which is the shape
this would eventually take if the vendored header were forked. Keys are dispatched as generic `keyDown`/`keyUp`
events to the **focused** node; `onFocus` and `onBlur` are the emitter's own. See *Focus and keyboard* below.

### The proof

```bash
hello_react --inject-pointer packages/core/test-bundles/pressable.js 200 140
```

Headless, for the same reason `--golden` is: no GPU, no Vulkan driver, no compositor, so it runs anywhere the unit
tests do. It boots the same `FabricHost` the window boots, waits for the bundle's first commit, and then delivers
three frames through the same `InputQueue`, `InputDispatcher` and `FrameEventBeat` the window uses — seventeen
motion events in the first, a press in the second, a release in the third — inducing the beat once per frame.

`packages/core/test-bundles/pressable.js` is a `<Pressable>` with the React removed: one 200x120 view at (100, 80)
declaring the pointer props `Pressability` declares, and a `registerEventHandler` callback that prints every event
Fabric hands it. Because there is no React, the instance handle is built by hand in the shape
`PointerEventsProcessor` resolves targets through — `instanceHandle.stateNode.node` — which is React's fiber
shape.

Expected output, in order:

```text
pressable: committed surface 1
pressable: topPointerOver on box at 200,140
pressable: topPointerEnter on box at 200,140
pressable: topPointerMove on box at 200,140
pressable: topPointerDown on box at 200,140
pressable: topPointerUp on box at 200,140
pressable: topClick on box at 200,140
```

The line that carries the batching claim is the single `topPointerMove`: seventeen motion events went in and one
came out, and the sixteen that did not arrive are the acceptance criterion. The `topPointerOver` and
`topPointerEnter` lines are the hover chain, and they are evidence the pipeline reaches
`PointerEventsProcessor` rather than bypassing it — neither event was ever dispatched by this platform.

### Deferrals, with owners

- **Touch and gestures.** `TouchEventEmitter::onTouchStart` and the responder system are untouched. Nothing on a
  desktop Wayland seat produces them without `wl_touch`, and a `PanResponder` needs the responder negotiation as
  well as the events. Not in M1.
- **Scroll** is no longer a deferral. `wl_pointer.axis`, `axis_stop` and `axis_discrete` are queued and routed to
  a `<ScrollView>` rather than to the pointer state machine, because a wheel moves a container and a click hits a
  node. `axis_source` is still ignored, and *ScrollView* says why.
- **Key repeat.** `wl_keyboard.repeat_info` is accepted and ignored, so a held key produces one `keyDown`.
  Synthesising repeat means a timer in the frame loop, which is the same machinery text input will need.
- **Focus traversal** is no longer a deferral. Tab order, the focus ring, `onFocus`/`onBlur` and Enter/Space
  activation are issues #37 and #38 and are implemented; *Focus and keyboard* below is the contract and its own
  deferral list.
- **A root instance handle.** `UIManager::startEmptySurface` does not give the root shadow node one, and
  `PointerEventsProcessor::getShadowNodeFromEventTarget` returns null without it, so an event whose target is only
  the root is dropped before the hover chain runs. The consequence is visible: moving off a view onto the
  background does not currently produce `pointerOut`. Fixing it means giving the root a fiber-shaped handle, which
  belongs with React Native's JavaScript surface registry rather than here.
- **IME.** `zwp_text_input_v3` is issue #26 and is implemented; see *IME* below. What is still deferred there is
  pre-edit **rendering**, which needs the `<TextInput>` of issue #17 to have something to render it in, and
  xkbcommon compose sequences and dead keys, which are a keyboard concern rather than an input-method one.
- **E2E traces.** Issue #18 also asks for hover/press traces and keyboard focus order under a headless compositor
  with virtual Wayland input. `--inject-pointer` is the unit-level and integration-level proof; the compositor-level
  one belongs with the harness that runs the lavapipe window golden, and neither exists yet.

## Focus and keyboard

Issues #37 and #38, milestone M1. One focused node per surface, Tab traversal, a focus ring, `onFocus`/`onBlur`,
Enter and Space activation, and a written-down key payload and routing rule. Both are prerequisites for the
`<TextInput>` of #17, and both exist because React Native's cross-platform surface has neither: thirteen
react-native-macos issues over six years are what a platform that invents `focusable`, an order, a ring and
activation keys separately and then keeps them consistent by hand costs.

```text
wl_keyboard ─▶ WaylandSeat ─▶ InputQueue ─┐                                    frame thread
   domKeyName, domKeyCode                 │
                       InputDispatcher ◀──┘
                             │ syncFocusables, once per commit
                             ▼
                        FocusModel ─┬─▶ ViewEventEmitter::onFocus / onBlur ─▶ EventQueue
                                    ├─▶ EventEmitter::dispatchEvent keyDown / keyUp
                                    ├─▶ TouchEventEmitter::onClick             (Enter, Space)
                                    ├─▶ LinuxMountingManager::setFocus         (the ring, and its damage)
                                    └─▶ TextInputFocusSink::enable / disable   (#17)
```

### What makes a node focusable, and why it is `accessible`

There is no `focusable` prop to read. Android and tvOS each declare one on their own `HostPlatformViewProps`;
the shared `BaseViewProps` the `cxx` platform aliases has none, and neither does `Props::rawProps`, which only
exists under `RN_SERIALIZABLE_STATE`. Adding one means a `platform/linux/.../HostPlatformViewProps.h` inside the
vendored tree, which is a fork of upstream headers and an ADR-level decision rather than an input one.

So the signal is **`accessible`**, from `AccessibilityProps`, minus **`accessibilityState.disabled`**. That is not
a workaround dressed up: `Pressable` already sets `accessible` on every control it renders, the web platform
draws the same equivalence between the interactive-and-exposed set and the tab order, and
react-native-macos#1655 is the bug filed when disabled controls stayed focusable. A `display: none` node is
skipped as well, because Yoga has already decided it is not on screen.

The set is read from the **committed shadow tree**, not from the retained scene. Both carry the tree and both
carry mount order, but only the shadow tree carries the event emitters `onFocus` and `onBlur` go through, so
reading the scene would cost a second lookup to emit anything. It is not a per-frame walk: a commit produces one
new root object, so an unchanged root pointer is an unchanged focusable set, and the walk runs once per commit.

### Traversal, and what the model owns

`FocusModel` in `src/FocusModel.cpp` is the part that can be arithmetically wrong, kept where the coverage gate
can see it — the same split `TextInputV3State` makes for composition. Its whole vocabulary is tags:

- **Order** is the caller's, and it is the pre-order walk of the committed tree, which is mount order and
  therefore already `zIndex` order because Fabric stable-sorts siblings by `getOrderIndex` before it diffs them.
- **Tab** moves forward, **Shift+Tab** backward, and both **wrap**. With nothing focused, forward starts at the
  first focusable and backward at the last, which is where a wrap from outside the list lands.
- **A click** moves focus to the deepest focusable on the path from the root to the hit node — so clicking the
  label inside a `<Pressable>` focuses the `<Pressable>` — and a click on anything else **blurs**, which is
  react-native-macos#999.
- **A commit that drops the focused node** blurs it. Focus does not walk to a neighbour: that needs a tree, and
  this class deliberately has none.
- Every transition names at most one blurred tag and one focused tag, and names neither when focus did not move,
  so `onFocus` and `onBlur` fire exactly once per change by construction rather than by a guard at the call site.

`tabIndex` is not implemented, because there is no prop to read it from; see the deferrals.

### Focus-visible, and where the ring is drawn

The ring follows the keyboard and not the pointer: Tab shows it, a click does not. That is the desktop
convention, it is CSS `:focus-visible`, and it is the difference between the two `FocusOrigin` values —
the model tracks the focused tag either way, so a click still decides where the next Tab goes.

`RetainedScene::setFocus` marks the focused primitive and `ScenePainter` strokes a 2 point ring in the platform
accent colour around that node's rounded border box. Two decisions are worth naming:

- The ring is **inside** the border box, not around it. Every damage rectangle in this renderer is a primitive's
  own transformed frame, cut by its inherited clips; an outset ring would paint outside the rectangle the scene
  damages for that node, and would need a term of its own in the damage math to stay correct through transforms
  and clips. react-native-macos#2063 is what a ring that disagrees with its own geometry looks like.
- A node that paints nothing else is **still painted for its ring**. A `<Pressable>` with no background is the
  common case, and a ring that only appeared on nodes the scene already had a reason to paint would be missing on
  exactly the controls that need it.

Damage is therefore the old node's frame plus the new one's, and the old one is damaged before the mark moves —
a node whose only reason to be painted was the ring has no extent once the mark has left it. A focus that draws
no ring damages nothing at all: a click changes where the next Tab starts and not a single pixel.

### The key payload, and where it diverges

`keyDown` and `keyUp` carry:

```text
{ key, code, ctrlKey, shiftKey, altKey, metaKey, repeat }
```

`key` and `code` are DOM values, computed by `domKeyName` and `domKeyCode` in `InputPipeline.cpp` where the
coverage gate scores them, from what xkbcommon already hands the seat. `domKeyName` consults three things in
order, and the order is the whole of the rule: the named-key table first, so Tab, Enter, Escape and Backspace do
not arrive as the control characters their keysyms also produce; a single-character **keysym name** next, so
`Ctrl`+`a` is still `a` rather than the U+0001 the modified text would be; and the **text** last, which is what
turns keysyms named `slash`, `exclam` and `eacute` into `/`, `!` and `é` without a table entry each. Anything
left over is `Unidentified`, which is the DOM's own answer. `ISO_Left_Tab` — what a keymap reports for
`Shift`+`Tab` — maps to `Tab`, because the DOM says the key is `Tab` and the shift is in the modifiers.

`code` is the physical key, from the evdev keycode, so it does not move when the keymap does. Divergences from
the two platforms this is meant to match, named here rather than discovered later — react-native-macos#702 is the
issue filed when they were not:

| Field | Here | react-native-macos | react-native-windows |
| --- | --- | --- | --- |
| `key` | DOM value | DOM-ish, subset of keys (#437) | DOM value |
| `code` | DOM physical code | absent | present |
| `ctrlKey`/`shiftKey`/`altKey`/`metaKey` | present | present | present |
| `repeat` | present, always `false` | present | present |
| `capsLockKey`, `numLockKey`, … | absent | present | absent |
| `nativeEvent.keyCode` | absent | absent | present |

`repeat` is always `false` because `wl_keyboard.repeat_info` is accepted and ignored — a held key produces one
`keyDown`. It is in the payload rather than absent from it so a component reading it never sees `undefined`.

### Routing, and the unhandled-key policy

Keys go to the focused node and to nothing else. A key pressed with nothing focused is **dropped**, and that is a
policy rather than an omission: the surface root has no instance handle — see the deferral in *Input* — so it
cannot be an event target, and on Wayland a key that reached this client is a key the compositor already routed
here, so there is nothing to escape to. react-native-macos#683 is what a platform that passes unconsumed keys
back to the system sounds like.

The key reaches React **before** the traversal or activation it may also trigger, so a Tab is visible to the node
that had focus rather than swallowed by the platform. Nothing can cancel that traversal: there is no return
channel from JavaScript on this path, which is the `preventDefault` deferral below.

Enter and Space on a focused node synthesise **the same `click` the pointer path produces**, built by the same
code, with the target's own origin as the coordinates so the offset inside the target is zero. `Pressability`
therefore turns them into `onPressIn`, `onPressOut` and `onPress` with no keyboard path of its own, which is what
react-native-macos#1622 was missing.

### The IME enable path

`TextInputFocusSink` in `InputPipeline.h` is the second half of the `ImeSink` seam. `ImeSink` is where a
composition lands; this is what decides whether a composition can start at all, and `zwp_text_input_v3` needs
both because `enable` is a request the client makes rather than a state the compositor infers. `TextInputClient`
implements it with the two methods it already had, and `InputDispatcher` calls them when focus enters or leaves a
component named `TextInput` — a name nothing registers yet, so issue #17 has to register it and nothing else.

`rnl_window --ime-debug` is unchanged and deliberately does **not** register the sink: it drives the same two
calls by hand, and both driving them would be two owners racing over one protocol object.

### The proof

```bash
hello_react --focus-tab packages/core/test-bundles/focus.js /tmp/rnl-focus.png 3
```

`packages/core/test-bundles/focus.js` is five views in a row: `alpha` and `beta` accessible, `gamma` with no
`accessible` at all, `delta` accessible but disabled, and `epsilon` accessible. Three Tab presses, one per frame
through the same `InputQueue` and `InputDispatcher` a window uses, and the trace is:

```text
focus: committed surface 1
focus: topFocus on alpha
focus: topKeyUp on alpha key=Tab code=Tab
focus: topKeyDown on alpha key=Tab code=Tab
focus: topBlur on alpha
focus: topFocus on beta
focus: topKeyUp on beta key=Tab code=Tab
focus: topKeyDown on beta key=Tab code=Tab
focus: topBlur on beta
focus: topFocus on epsilon
focus: topKeyUp on epsilon key=Tab code=Tab
```

Four things in there are the claims. The first press produces **no** `topKeyDown`, because nothing was focused
yet and an unfocused key is dropped — that is the unhandled-key policy, visible. Every press after it produces a
`topKeyDown` on the node that was focused *before* the traversal, which is the ordering rule: the key reaches
React before the traversal it triggers. The third `topFocus` is `epsilon` rather than `gamma`, which is the
focusable filtering — one view was never accessible and the next was disabled. And every `topBlur` is immediately
followed by exactly one `topFocus`, which is the double-emit prevention.

Adding a Space press to the same fixture adds one line, `focus: topClick on epsilon`, which is the activation
click: the same event the pointer path produces, and therefore the same `onPress` on the JavaScript side.

The golden `packages/core/goldens/focus.png` is the same run's picture: the ring on `epsilon`, the fifth view.
It is registered in `goldens/golden.spec.ts` alongside the scroll fixture and regenerates with
`pnpm test:golden:update`.

### Tests

`FocusModel.cpp` is in `scopedSourcePaths` at 100% line and branch, covered by `packages/core/tests/FocusTest.cpp`:
traversal order, both wraps, Shift+Tab, click-to-focus, blur-on-background, unmount-clears, reorder-across-commit,
keyboard-origin versus pointer-origin visibility, and the traversal, activation and text-component key rules.
`InputTest.cpp` covers the key naming and the activation click; `SceneTest.cpp` covers the focus mark, the ring on
a node that paints nothing else, and that a focus change damages exactly the old and the new frame.
`InputDispatcher` itself is outside the gate for the reason `TextInputClient` and `WaylandSeat` are: what is left
in it is Fabric plumbing that needs a `UIManager` and a committed tree, and `--focus-tab` is the test for it.

### Deferrals, with owners

- **`focusable`, `tabIndex` and `nextFocus*` as props.** All three need a platform `HostPlatformViewProps` inside
  the vendored tree, which is a fork of upstream headers and belongs in an ADR rather than in this issue.
  `accessible` is what stands in for `focusable`; `tabIndex` is why traversal is mount order and only mount order.
- **Programmatic `focus()`, `blur()` and `isFocused()`.** react-native-macos#518 and #913. They arrive as Fabric
  commands through `IMountingManager::dispatchCommand`, which is implemented as a no-op here, and they need a
  JavaScript surface to be called from. Not on the M1 path.
- **Directional navigation and focus zones.** Arrow-key movement needs a geometric model of "the next control to
  the right", and focus zones need containers that trap it. Neither is on the M1 path and the Prime Directive
  says the second consumer has to exist first.
- **Accessibility focus.** The screen-reader cursor is a second focus with its own order, and there is no
  accessibility tree here at all — no AT-SPI bridge, no `accessibilityRole` mapping. A separate subsystem.
- **`preventDefault` on a key.** A component cannot cancel a traversal or an activation, because Fabric's event
  path has no return channel a platform can read synchronously. `validKeysDown`/`validKeysUp` — Windows' answer,
  where a component declares the keys it wants — is the same deferral: it is a prop, and props are the fork above.
- **Key repeat.** `wl_keyboard.repeat_info` is still accepted and ignored, so `repeat` is always false. Named in
  *Input* as well; it needs a timer in the frame loop.
- **Focus during composition.** No key is suppressed while a `zwp_text_input_v3` composition is active. *IME*
  explains why the platform does not second-guess the compositor, and until #17 there is nothing that inserts
  text from a key event, so nothing can double a character. The rule and its test land with the field.
- **The ring's colour.** Fixed accent, not the compositor's. Reading the system accent is an
  `org.freedesktop.portal.Settings` round trip and a theming subsystem, and there is no other themed value yet.
- **Clipped-out nodes.** A focusable scrolled out of an `overflow: hidden` ancestor is still in the tab order.
  Skipping it means asking the scene for visibility during a shadow-tree walk, which is the one place the two
  models would have to agree; it waits for a case that needs it.

## IME

Issue #26, milestone M1. Composition through `zwp_text_input_v3`, which ADR-0001 makes a prerequisite of
`<TextInput>` rather than an accessibility afterthought: most of the world's languages cannot be typed at all
without it, so a text field that has keys but no input method is a text field for English.

Nothing here draws a candidate window. On Wayland the compositor's input method owns that popup, and the only
thing a client owes it is the rectangle around the cursor to avoid — which is the single largest reason this is a
few hundred lines rather than a subsystem.

```text
zwp_text_input_v3 ─▶ TextInputClient ─▶ TextInputV3State ─▶ InputQueue ─┐   frame thread
   preedit_string                          batches until done           │
   commit_string                                                        │
   delete_surrounding_text                       InputDispatcher ◀──────┘
   done(serial)                                        │ ImeSink
                                                       ▼
                                              the focused text field (#17)
```

### Binding, and which version

`WaylandWindow` binds `zwp_text_input_manager_v3` from the registry and asks the seat for one `zwp_text_input_v3`
— the object is per seat, not per surface, because text-input focus follows the seat's keyboard focus. A
compositor that does not advertise the manager leaves `WaylandWindow::textInput` null and the window keeps
working with keys alone; that is not hypothetical, it is what a bare weston without an input method does.

The manager is bound at **version 1**. The protocol XML lives at
`$(pkg-config --variable=pkgdatadir wayland-protocols)/unstable/text-input/text-input-unstable-v3.xml` — under
`unstable/`, not `stable/`, because v3 is still an unstable protocol and its `z` prefix says so. wayland-protocols
1.49 raised the interface itself to version 2, which adds `action`, `language` and `preedit_hint` events plus
`set_available_actions`, `show_input_panel` and `hide_input_panel`. Binding version 2 would oblige us to answer
events no component exists to render, so version 1 it is; the generated listener struct still carries the version 2
members, which is exactly why `TextInputClient::makeTextInputListener` value-initialises the struct and fills it
member by member instead of using a designated initialiser. `wayland-scanner` generates the client header and the
private code into `build/<preset>/packages/core/wayland-protocols`, into `rnl_text_input`, mirroring what
`rnl_xdg_shell` already does for xdg-shell. Nothing generated is checked in.

### Everything is double-buffered, in both directions

v3 sends a composition in pieces and none of them mean anything on their own. `preedit_string`, `commit_string`
and `delete_surrounding_text` each modify pending state; `done` replaces the current state with all of it at once
and resets the pending values to initial. Applying a piece when it arrives is not a shortcut, it is a different
protocol, and it is what produces the duplicated and reordered characters that IME bug reports are made of.

`TextInputV3State` is that buffer, as a pure class with no Wayland types in it, and `applyDone` returns the
`InputEvent`s in the order the protocol's own `done` description evaluates them:

1. replace the existing pre-edit with the cursor,
2. delete the requested surrounding text,
3. insert the commit string with the cursor at its end,
4. insert the new pre-edit and place the cursor inside it.

So a batch that deletes, commits and re-composes yields `ImeDeleteSurrounding`, then `ImeCommit`, then
`ImePreedit`, and a text buffer that applies them in arrival order is correct by construction. A pre-edit that
changed only its cursor pair is still an event, because an input method moving the highlight through a candidate
is telling the field to repaint. `-1, -1` means the cursor is hidden.

Requests are double-buffered the same way: `enable`, `set_surrounding_text` and `set_cursor_rectangle` are all
pending until a `commit`, so `TextInputClient` caches what it was told and issues the three together. A text field
never has to know that `commit` exists.

Two empty values are protocol-significant rather than harmless. An empty surrounding text means "this client does
not support surrounding text", and an all-zero cursor rectangle means "this client does not know where its cursor
is" — and the protocol warns that once the empty value is applied, later attempts to change it may have no effect.
Neither is sent until there is something real to say.

### The serial, and what a mismatch means

The compositor counts our `commit` requests and sends that count back as the serial on `done`. A serial that is
not our own count means the compositor answered a state we have already replaced: the composition still applies —
the user's keystrokes are not negotiable — but our own state requests wait for a `done` whose serial matches
before they are sent again. That is `needsStateResend`, and `TextInputClient` re-sends the cached state on the
first matching `done`. `enable` and `enter` clear the gate, because the `commit` that carries an `enable` cannot
wait for a serial that only another `commit` would produce.

### Focus, and what enabling costs

`enter` and `leave` follow the compositor's keyboard focus. Both invalidate every piece of state the protocol
carries, in both directions: after either one the compositor knows nothing about this text input, the pre-edit on
screen is gone, and a field that wants composition must `enable` again and re-send its state. `leave` therefore
emits an empty `ImePreedit` when a composition was on screen, so the field clears it rather than leaving a
half-composed word behind.

`enable` is refused while the text input has no focus, because the protocol says the compositor ignores every
request from a text input that has not been sent `enter`.

### Keys during composition

While a composition is active the compositor may still send `wl_keyboard.key` events, and the rule for React is
that **key events are not text**. Text arrives only as `ImeCommit`. A `keyDown` that arrives during a pre-edit is
either a key the input method did not consume or one the compositor chose to forward, and a `<TextInput>` that
inserts characters from key events as well as from commits will double every character the moment an input method
is running. The platform does not filter those keys — with fcitx5 under a wlroots compositor the keyboard grab
means most of them never arrive in the first place, and second-guessing which ones did would be a filter that
disagrees with the compositor.

### How `<TextInput>` plugs in

`ImeSink` in `InputPipeline.h` is the contract: `onImePreedit`, `onImeCommit`, `onImeDeleteSurrounding`.
`InputDispatcher::setImeSink` registers the platform-level focus owner, and composition events are the one input
that is not hit-tested — the target is whatever holds the text cursor, not whatever is under the pointer. Issue
#17 makes the focused field that owner and gives `TextInputClient::setSurroundingText` and
`setCursorRectangle(x, y, width, height)` real values: the text around the caret, and the caret's rectangle in
surface-local coordinates so the candidate window lands beside it instead of on top of it. Until then the only
implementation is the debug sink below, and an unregistered sink means composition events are dropped rather than
queued.

### The proof, without a `<TextInput>`

```bash
./build/dev/bin/rnl_window --ime-debug
```

`--ime-debug` enables the text input on the window itself as soon as the compositor gives it focus, reports a stub
surrounding text and a fixed 2x24 caret rectangle at (64, 64), and prints every composition batch. It composes
with `--fabric` and `--screenshot`; on a compositor with no text-input manager it says so on stderr and carries on.

With fcitx5 running and its input method switched to Pinyin, focusing the window and typing `nihao` then space
prints:

```text
[rnl-ime] enabled on the focused surface
[rnl-ime] preedit "n" cursor 1..1
[rnl-ime] preedit "ni" cursor 2..2
[rnl-ime] preedit "niha" cursor 4..4
[rnl-ime] preedit "nihao" cursor 5..5
[rnl-ime] commit "你好"
[rnl-ime] preedit "" cursor 0..0
```

The last two lines are the whole claim: the commit and the pre-edit that ends the composition arrive in one
`done` batch and in that order, and the empty pre-edit is what tells a field to remove the composing run. The
exact pre-edit strings are the input method's business — fcitx5's Pinyin engine shows the typed letters, Anthy
and Hangul engines show composed syllables instead — so the shape of the sequence is the assertion, not the
strings.

### Manual checklist, Hyprland with fcitx5

`fcitx5` is not a build dependency and CI never installs it; this is a developer-machine check. On Arch:
`sudo pacman -S --needed fcitx5 fcitx5-configtool fcitx5-chinese-addons`, then run `fcitx5` with
`fcitx5 --replace -d`. It needs no `GTK_IM_MODULE` or `QT_IM_MODULE` for this: those environment variables are for
toolkit clients, and a Wayland text-input client is reached through the compositor.

1. `pnpm --filter @react-native-linux/core run:window:ime` opens the usual placeholder window and, once the
   compositor focuses it, prints `[rnl-ime] enabled on the focused surface`. Nothing else is printed while typing
   with the input method off.
2. Switch to Pinyin with fcitx5's toggle (`SUPER SPACE` on stock Omarchy) and type `nihao`. The candidate window
   appears **beside the caret rectangle**, near (64, 64) in the window rather than at the window's corner or the
   pointer, and each letter prints a `preedit` line.
3. Press space. One `commit` line with the composed characters, immediately followed by an empty `preedit` line.
   No further output until the next composition.
4. Press escape mid-composition. The pre-edit is abandoned with an empty `preedit` line and no commit.
5. Click another window and come back. Focus loss prints an empty `preedit` line if a composition was open, and
   the return prints `enabled on the focused surface` again — that is `enter` invalidating all state and the
   client re-enabling, which is the sequence a compositor is entitled to require.
6. Switch the input method back off and type ASCII. Nothing is printed, because plain keys are not composition;
   they arrive as `keyDown`/`keyUp` and go to the pointer's node. See *Input*.
7. `./build/dev/bin/rnl_window --ime-debug --fabric packages/core/test-bundles/fabric-view.js` behaves the same
   with a bundle loaded, and the bundle's own output is unaffected.
8. On a compositor without the manager — `weston --backend=headless` is one —
   `[rnl-window] the compositor does not advertise zwp_text_input_manager_v3` goes to stderr and the window still
   opens and still closes cleanly.

### The compositor and input-method matrix

The client half of this is uniform; the half that draws the candidate window is not.

| Compositor | `text-input-v3` | `input-method-v2` | What that means here |
| --- | --- | --- | --- |
| Hyprland, sway, wlroots | yes | yes | fcitx5 runs as the input method and draws its own popup. The reference configuration for this checklist. |
| KDE / kwin | yes | yes | Same, and kwin also ships its own virtual keyboard path. |
| GNOME / mutter | yes | **no** | Composition works, but fcitx5 cannot render a candidate window without the `kimpanel` shell extension. That is a GNOME limitation, not something a client can fix. |

ibus is the other common input method and speaks `input_method_v1` on Wayland, so under wlroots compositors it
effectively only serves XWayland clients; fcitx5 is the one to test against. None of this changes what this client
sends — it is the same protocol either way — which is the argument for having implemented v3 and nothing else.

### Tests

`TextInputV3State` is a pure class for exactly one reason: it is the part that can be arithmetically wrong, so it
belongs where the coverage gate can see it. `packages/core/tests/ImeTest.cpp` covers the batching order, the
last-preedit-wins rule, the cursor-pair change, the composition-ending empty pre-edit, focus invalidation and the
serial mismatch, and `TextInputV3State.cpp` is in `scopedSourcePaths` at 100% line and branch. `TextInputClient.cpp`
is outside that scope for the same reason `WaylandSeat.cpp` is: what is left in it is protocol plumbing that needs
a compositor, and `--ime-debug` is the test for it.

The e2e layer issue #26 asks for — a virtual input method injecting composition under the headless compositor —
is not built. It needs the harness to speak the compositor side, `input-method-v2`, which is a second protocol
implementation and the *Deferrals* below explain why it is not this issue's.

### Deferrals, with owners

- **Pre-edit rendering.** Underlined, highlighted composing text inside a text field is issue #17's, because
  there is no field to render it in. `ImePreedit` already carries the cursor pair the styling needs.
- **`input-method-v2`.** The compositor side of the protocol — being the input method rather than talking to one
  — is what a virtual-IME e2e test and any in-process candidate window would need. It is not on the M1 path and
  the Prime Directive says a second protocol implementation waits for a second reason to exist.
- **`text-input-unstable-v1` and XIM.** Neither is implemented and neither is planned. v1 is the GTK-era protocol
  that v3 replaced, and XIM is X11's, which reaches this platform only through XWayland — which this platform
  does not use. ADR-0001's Wayland-only decision is what makes that a closed question rather than an open one.
- **Content hints and purpose.** `set_content_type` maps to `<TextInput>`'s `keyboardType`, `autoCapitalize`,
  `secureTextEntry` and `autoComplete` props, so it lands with the props, in #17.
- **`set_text_change_cause`.** The client must tell the input method when the text changed for a reason other
  than composition. That needs a text buffer that can change for another reason, which is #17.
- **Compose sequences and dead keys.** `xkb_compose_state` turns `dead_acute` + `e` into `é` without any input
  method running. It is xkbcommon's, it belongs beside the keymap in `WaylandSeat`, and it is a keyboard feature
  that composition does not supply.
- **Version 2.** `preedit_hint` would let an input method style parts of a pre-edit, and `language` would let a
  field follow the input method's language. Both need a rendering field first.

## Golden images

The rig has two halves. `hello_react --golden` produces a PNG; a Vitest spec compares it against a checked-in one.

```text
hello_react --golden <bundle> <output.png> [width height]
hello_react --damage-golden <bundle> <output.png> [width height]
```

The default size is 800x600, the same constraint the headless Fabric surface uses. The run is the ordinary headless
one — boot the bridgeless instance, boot `FabricHost`, load the bundle, wait for the JavaScript thread to go quiet —
and then it paints `runFabricBundle`'s scene snapshot through `paintScene`, the same function `rnl_window` calls per
frame, into a surface from `SkSurfaces::Raster` at `kRGBA_8888_SkColorType` and `kPremul_SkAlphaType`. The pixels are
encoded with `SkPngEncoder::Encode(SkWStream*, const SkPixmap&, const Options&)` into an `SkFILEWStream`.

### Why the render is offscreen, and what that defers

The gospel calls for lavapipe under a headless Wayland compositor, and issue #6's acceptance criteria say so. This
slice does not do that, deliberately:

- The rig has to run where screen capture is permission-gated. On the Omarchy/Hyprland development machine
  `grim`/`wlr-screencopy` is gated, so a full-window capture is not available at all.
- `SkSurfaces::Raster` needs no GPU, no ICD, no compositor and no display, so the rig runs unchanged in a CI
  container with nothing graphical installed. That is the Prime Directive answer: the minimal thing that makes
  rendering observable.
- The cost is honest and bounded. A raster golden proves the scene, the geometry and the paint code path. It does
  **not** prove `SkiaVulkanRenderer`: swapchain wrapping, image layout transitions, semaphore handling, colour
  space through the WSI, or `wl_surface` presentation. Every one of those is what the lavapipe plus
  `weston --backend=headless` rig covers; it landed separately, as issue #33. See *Window goldens*.

### Link impact on hello_react

`libskia.a` is a static archive, so the linker pulls only the objects the golden path references. Nothing in
`ScenePainter.cpp` or `GoldenRenderer.cpp` names `GrDirectContext`, `GrBackendRenderTargets` or any `vk*` entry
point, so no Ganesh or Vulkan object is pulled and `hello_react` needs no Vulkan loader. libpng and zlib are
compiled **into** the archive (`png_create_write_struct` and `deflate` are defined there, not undefined), so PNG
encoding adds no system dependency either. The only new link inputs are freetype and fontconfig, which Skia's font
ports reference and which `rnl_window` already required; they are what `RNL_ENABLE_SKIA` checks for alongside the
archive itself.

`rnl_scene_painter` still carries `SK_GANESH;SK_VULKAN;SK_RELEASE`, unchanged from what the window target used to
set. Those are header-layout switches against a prebuilt archive, not feature switches for this build: dropping
`SK_VULKAN` for the golden path would compile the shared painter against a different ABI than `rnl_window` does.
The reason `SK_RELEASE` is mandatory even in the Debug preset is item 2 of the window fix ledger below.

### The comparison, and why the tolerance is zero

`packages/core/goldens/png-diff.ts` compares two decoded RGBA buffers channel by channel and reports the first
differing pixel by coordinate. `packages/core/goldens/png-diff.spec.ts` covers it; it needs no binary and runs
everywhere. Decoding is `pngjs`, a maintained dependency-free PNG codec, added to `@react-native-linux/core`'s
devDependencies through the workspace catalog.

The gospel's perceptual threshold exists because GPU rasterisation and font hinting vary between drivers. Skia's
raster backend does not: one Skia build given one scene produces the same bytes on every machine, so the threshold
here is exact equality. A tolerance would only hide a real regression. The lavapipe window golden has its own
comparator with its own stated thresholds, `packages/core/goldens/perceptual-diff.ts`, rather than a loosened
version of this one; see *Window goldens*.

### The workflow

`packages/core/goldens/golden.spec.ts` runs under the ordinary `pnpm test`. It builds nothing. When
`build/dev/bin/hello_react` is absent it prints why and skips, so a checkout without a C++ build stays green.

Regenerating is explicit and never happens as a side effect of a normal run:

```bash
cmake --build build/dev --target hello_react
pnpm test:golden:update   # RNL_UPDATE_GOLDENS=1, writes packages/core/goldens/*.png
pnpm test:golden          # compares; must pass immediately afterwards
```

**A regenerated golden is reviewed like code.** Open the PNG, confirm it is the picture the change intended, and
say in the pull request why it changed. A golden that is updated because "the test failed" is a deleted test. When
a golden is missing and `RNL_UPDATE_GOLDENS` is unset the spec fails and names the regeneration command rather than
silently creating a baseline.

The first fixture is `packages/core/test-bundles/fabric-view.js` to `packages/core/goldens/fabric-view.png`: the
dark `#14161A` background with one flat blue `#3366CC` rectangle, 120x80, its top-left corner at (24, 24) — the
frame `hello_react --fabric` prints for `View #4`, rasterised instead of dumped.

The second is `packages/core/test-bundles/view-props.js` to `packages/core/goldens/view-props.png`, the fixture for
*View props fidelity* above. Every element is positioned absolutely so the picture is fixed, and each one isolates
one prop group. Left to right, top to bottom, on the same `#14161A` background:

1. (40, 40) 160x120 blue `#3366CC` with a 40 px radius on the **top-left and bottom-right corners only** — the
   other two stay square.
2. (230, 40) 160x120 dark `#1E2430` with a uniform 10 px amber `#E5C07B` border and a 24 px radius on all corners.
   The border is drawn inside the frame, so the tile is still exactly 160x120.
3. (420, 40) 160x120 dark `#1E2430` with four different borders: 6 px red `#E06C75` left, 12 px green `#98C379`
   top, 18 px blue `#61AFEF` right, 24 px purple `#C678DD` bottom, meeting on the diagonal at each corner.
4. (610, 70) 150x60 cyan `#56B6C2` asking for a 200 px radius. The corner-overlap clamp reduces it to 30 px, so
   this must render as a **perfect pill**, not a lozenge and not a rounded rectangle with visible flat corners.
5. (40, 200) 200x140 red `#E06C75` at `opacity: 0.5`, containing a 140x80 white block at `opacity: 0.5` inset 30 px
   from its top-left. The white block therefore paints at an effective 0.25 alpha.
6. (280, 200) 200x140 dark `#1E2430`, 28 px radius, `overflow: hidden`, containing a 300x220 green `#98C379` child
   offset 60 px down and right. The green must stop exactly on the parent's rounded edge, leaving a dark L-shaped
   band along the top and left.
7. (520, 200) 160x110 blue `#61AFEF`, 12 px radius, rotated -15° and scaled 1.15 **about its own centre**.
8. (40, 380) 340x200 slate `#1A1F2B` panel holding three overlapping 150x150 squares declared red, green, blue in
   that order and carrying `zIndex` 3, 1, 2. The stack must read green at the bottom, blue in the middle, **red on
   top**. A renderer that ignored `zIndex` would put blue on top instead, which is what makes this element a test.

The third is `packages/core/test-bundles/text.js` to `packages/core/goldens/text.png`, the fixture for *Text*
above. It needs `pnpm --filter @react-native-linux/core vendor:fonts` to have run; without the vendored faces the
paragraphs are shaped with whatever fontconfig finds and the comparison fails, which is the correct outcome. Every
string is ASCII so that nothing falls through to a system font. On the same `#14161A` background, top to bottom:

1. (40, 32) a 32 px bold white `#F2F4F8` heading, `Text renders on Linux`, on one line.
2. (40, 96) a 300 px wide `#1E2430` panel with 12 px padding holding a 16 px, 24 px line-height paragraph that
   **wraps onto several lines**. Its fragments are white, bold amber `#E5C07B`, white, italic green `#98C379`,
   white — proving per-fragment attributes survive the flattening into one `AttributedString`, and that a bold or
   italic run inside a line does not restart the line box.
3. (380, 96) a 380 px wide panel holding a 16 px muted `#9AA4B2` paragraph with `numberOfLines: 1`, which must be
   **cut on the first line and end in a `…`** rather than wrapping or being clipped mid-glyph.
4. (40, 300) and (40, 340) two 20 px sky `#61AFEF` lines in a 720 px wide box, one `textAlign: center` and one
   `textAlign: right`. Their left edges must differ from each other and from every other line in the picture.
5. (40, 396) an 18 px white line with `letterSpacing: 6`, visibly tracked out.
6. (40, 440) an 18 px amber line with `textDecorationLine: underline`.

The heading and the two aligned lines carry no `backgroundColor` and no border, so they also prove the scene's
visibility rule: a node that paints only text is still emitted as a primitive.

The fourth is `packages/core/test-bundles/image.js` to `packages/core/goldens/image.png`, the fixture for *Image*
above. It needs `pnpm --filter @react-native-linux/core assets:test-image` to have produced
`packages/core/assets/rnl-test-image.png`; that file is checked in, so the command is only needed when the asset
itself changes. The asset is a 64x48 PNG with a 2 px light `#F2F4F8` border, four quadrants — red `#E06C75` top
left, green `#98C379` top right, blue `#61AFEF` bottom left, amber `#E5C07B` bottom right — and an 8x8 dark
`#14161A` square in the middle. Its 4:3 aspect is what makes `cover` and `contain` produce visibly different
rectangles inside the fixture's 130x120 tiles.

Every tile is 130x120 on a `#1E2430` panel, so whatever the image does not cover reads as panel. On the same
`#14161A` background, left to right, top to bottom:

1. (30, 40) `cover`. The image scales 2.5x to 160x120, fills the tile top to bottom, and is **cropped left and
   right**: no panel is visible and the light border survives only along the top and bottom edges.
2. (182, 40) `contain`. The image scales 2.03x to 130x97.5 and is centred, leaving a **panel band above and
   below** and the whole light border intact.
3. (334, 40) `stretch`. The image fills all 130x120 with its aspect ratio distorted; the centre square is a
   rectangle.
4. (486, 40) `center`. The image is drawn at its natural 64x48 in the middle of the tile, surrounded by panel on
   all four sides.
5. (638, 40) `repeat`. The image tiles from the tile's **top-left corner** at natural size: two full columns and
   two full rows, then a partial third of each cut off by the frame.
6. (30, 200) the `data:` URI at `stretch`: a 4x4 image of four colour quadrants blown up to 130x120 with the
   bilinear filtering iOS and Android also apply, so the tile reads as the four colours blending into each
   other across a soft cross, not as four hard-edged rectangles.
7. (182, 200) the same `data:` URI at `repeat`: the 4x4 tile repeated, which reads as a fine regular checker.
8. (334, 200) `cover` with `borderRadius: 28`. Same picture as tile 1, with **all four corners cut away** to the
   panel-free background.
9. (486, 200) `cover` with `borderRadius: 20` and an 8 px uniform amber `#E5C07B` border, which is drawn **over**
   the image and inside the frame, so the tile is still exactly 130x120.
10. (638, 200) `cover` at `opacity: 0.4`, so the whole tile is the same picture as tile 1 blended toward the
    background.
11. (30, 360) `contain` with `tintColor` sky `#61AFEF`. The image becomes a flat sky-blue silhouette of itself:
    every opaque pixel is the same colour, so the quadrants and the centre square disappear and only the shape
    and the letterboxing remain.
12. (182, 360) an `https://` source. **Nothing but the panel**, because there is no networking stack; the run
    prints one `[image] unsupported source` line to stderr, which is the graceful-degradation path being proved
    rather than a failure.

The fifth is `packages/core/test-bundles/scroll.js` to `packages/core/goldens/scroll.png`, and it is the one
fixture that is not rendered by `--golden`: a `<ScrollView>` at rest at zero would prove nothing that a `<View>`
with `overflow: hidden` does not already prove, so it is rendered by `--scroll-to` after three wheel notches over
(160, 100). The picture, the geometry behind it and why the marker rectangle is the clip assertion are all in
*ScrollView*.

The sixth is `packages/core/test-bundles/focus.js` to `packages/core/goldens/focus.png`, and it is not rendered by
`--golden` for the same reason: a focus ring only exists after a traversal, so it is rendered by `--focus-tab`
after three Tab presses. Five views in a row, the ring on the fifth, because the third declared no `accessible`
and the fourth is disabled — the picture is the traversal order and the focusable filtering at once. See
*Focus and keyboard*.

### The partial-redraw equivalence proof

The last fixture is different in kind: `--damage-golden` is issue #12's acceptance criterion — "partial redraw
equals full redraw" — turned into an assertion, and the PNG is a by-product.

`packages/core/test-bundles/damage.js` commits twice. The first commit is an ordinary frame. A `setTimeout`
callback then commits a second tree, so it arrives as its own mounting transaction rather than as a second tree in
the same commit, and it does three things at once: it moves and recolours one view, it unmounts another, and it
leaves a third untouched.

`renderDamageGolden` paints the first commit's scene in full onto **two** raster surfaces, then paints the second
commit's scene over one of them in full and over the other clipped to the damage the scene accumulated — the same
`paintScene` call the window makes, minus the swapchain. The two surfaces are compared pixel by pixel in C++ and a
single mismatch fails the run with its coordinate. Only then is the **damage-clipped** surface written, so the
checked-in golden is the partial redraw rather than a full one that happens to match it.

Under-damage and over-damage both fail, and each of the fixture's three elements is there to catch one way of
being wrong:

- The **moved** view fails the comparison if the position it left is not damaged: its old green rectangle would
  survive the partial redraw and not the full one.
- The **unmounted** view fails it if a delete does not damage the node's old extent.
- The **untouched** view proves the clip is doing something: it is outside every damage rectangle, so its pixels in
  the damage-clipped surface come from the first frame and have to match what the full redraw painted from scratch.

There is no C++ equivalent of "the damage was suspiciously large", and there does not need to be — an oversized
damage region is a wasted repaint, not a wrong picture, and the merge policy is unit-tested where it lives.

The run is timing-sensitive in one place and fails loudly rather than silently there: the host has to observe the
first commit before the timer fires the second one, which it does by polling the scene after loading the bundle.
If it ever missed that window, the second commit's damage would already have been taken and discarded, and the run
exits 1 with `the bundle produced no damage after its first commit` instead of writing a golden that proves
nothing. The bundle's one-second delay is the margin.

The expected picture, on the same `#14161A` background, is the second frame:

1. (60, 50) 180x130 blue `#3366CC` — the untouched view, unchanged from the first frame.
2. (420, 260) 140x100 amber `#E5C07B` — the moved view, which was green `#98C379` at (300, 200) in the first frame.
3. Nothing at (300, 200) and nothing at (600, 80): the moved view's old position and the unmounted view's
   rectangle are both damaged, cleared, and left as background.

## Window goldens

The raster rig above proves the scene and the paint code path on the CPU. This one proves the half it cannot
reach: `SkiaVulkanRenderer` wrapping swapchain images as `SkSurface`s, the image layout transitions, the acquire
and render semaphores, the format the WSI negotiated, and `vkQueuePresentKHR` actually committing a `wl_surface`.
It is issue #33, and it is the gospel's "lavapipe software Vulkan + headless Wayland compositor" clause taken
literally. Three parts: a screenshot mode in the binary, a compositor rig, and a comparator that is not the raster
one.

```text
rnl_window [--fabric <bundle>] --screenshot <output.png> [--frames N]
```

`--frames` defaults to 60, which under a 60 Hz headless output is about a second — long enough for the bundle to
load, mount and settle before the frame that is captured. Without `--screenshot` the frame count is ignored and
the window runs until it is closed, exactly as before.

### The readback

The capture is a mode of `drawFrame`, not a separate pass over an image that has already been presented, and that
is a correctness point rather than a convenience. Between `vkAcquireNextImageKHR` and `vkQueuePresentKHR` the
application owns the image and knows its layout; afterwards it owns neither, and re-acquiring returns whichever
image is free rather than the one just shown. `captureNextFrame` therefore arms a path that runs after Skia's
`flush`/`submit` and before the present request:

1. A host-visible, host-coherent `VkBuffer` of `width * height * 4` bytes, from the first memory type that accepts
   the buffer and carries both properties.
2. A transient command pool and one primary command buffer.
3. A barrier from `VK_IMAGE_LAYOUT_PRESENT_SRC_KHR` to `VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL`, with
   `VK_PIPELINE_STAGE_ALL_COMMANDS_BIT` as the source stage. `PRESENT_SRC_KHR` is the layout Skia was asked for
   through the `MutableTextureState` on the flush, so it is the layout the image is in. The barrier's first
   synchronisation scope covers everything already submitted to this queue, which is what orders the copy after
   Skia's rendering without adding a second semaphore to the frame.
4. `vkCmdCopyImageToBuffer` with `bufferRowLength` and `bufferImageHeight` left at zero, so the rows are tightly
   packed and the stride is `width * 4`.
5. A barrier back to `PRESENT_SRC_KHR`, restoring exactly the layout Skia recorded, so both the present that
   follows and Skia's own tracking stay valid.
6. Submit with a fence, wait on the fence, map the memory.

A pending capture also suppresses the idle path in *Damage tracking*, so the captured frame is always painted and
always submitted through Skia. Two things follow. The picture is a **full repaint** — an image that owed nothing
has an empty damage list, and an empty list is what `paintScene` treats as no clip at all — so a screenshot never
depends on the partial-redraw arithmetic being right, which the raster `--damage-golden` rig proves separately.
And the readback is ordered after real submitted work, which is what the `ALL_COMMANDS` barrier above needs; an
empty submission would give it nothing to order against.

Format handling is explicit. The swapchain is whichever of `VK_FORMAT_B8G8R8A8_UNORM` or `VK_FORMAT_R8G8B8A8_UNORM`
the surface offered — B8G8R8A8 wins the preference loop, so in practice the mapped bytes are BGRA. The mapped
memory is wrapped in an `SkPixmap` carrying the swapchain's own `SkColorType`, and `SkPixmap::readPixels` converts
it into a second `kRGBA_8888_SkColorType` pixmap that `SkPngEncoder` writes. The swizzle is therefore Skia's
conversion rather than a hand-rolled byte shuffle, and the golden does not depend on which format the surface
happened to offer. `kTopLeft_GrSurfaceOrigin` is what the surfaces are wrapped with and image row 0 is the top row,
so nothing is flipped.

A failure at any step throws, and `WindowMain` turns that into a `[rnl-window]` line and exit 1. If a frame
rebuilds the swapchain instead of painting — a resize, or `VK_ERROR_OUT_OF_DATE_KHR` — the request stays pending
and the next frame takes it, so a compositor that reconfigures the window during startup delays the capture
instead of losing it.

### The compositor rig

`scripts/window-golden.ts` is the rig, and it renders; it does not compare. It creates a private
`XDG_RUNTIME_DIR` under `TMPDIR`, starts

```text
weston --backend=headless --no-config --socket=<private> --width=800 --height=600 --idle-time=0
```

with `DISPLAY`, `WAYLAND_DISPLAY`, `VK_ICD_FILENAMES` and `VK_DRIVER_FILES` stripped from the inherited
environment, waits for the socket to appear in that directory, then runs `rnl_window --fabric <bundle>
--screenshot <out> --frames 60` once per fixture with `WAYLAND_DISPLAY` and `XDG_RUNTIME_DIR` pointed at the
private compositor and `VK_ICD_FILENAMES` pinned to the lavapipe manifest it found under
`/usr/share/vulkan/icd.d` or `/etc/vulkan/icd.d`. Then it kills weston and removes the directory.

The compositor's own renderer is irrelevant, which is worth stating because it is the reason this rig is cheap:
the golden's pixels come out of the swapchain image, not out of anything weston composited, so weston only has to
accept and release buffers. Mesa's Wayland WSI uses `wl_shm` for lavapipe, since there is no DRM device to import
a dma-buf from, and every compositor advertises `wl_shm`.

Only weston is supported. `cage` and `sway` were considered and rejected for this rig rather than deferred: cage
launches a client instead of offering a socket, which is a different shape, and adding a second compositor before
the first one has failed anywhere is the speculative generality AGENTS.md forbids.

The script exits **2**, with the reason on stderr, when `build/dev/bin/rnl_window`, `weston` or the lavapipe ICD is
missing. That is what keeps a checkout without a graphics stack green, and it is one detection path rather than one
in the script and another in the spec.

### The comparator, and why zero tolerance is wrong here

`packages/core/goldens/perceptual-diff.ts` is a second comparator beside `png-diff.ts`, not a loosened version of
it. The raster goldens compare byte for byte because Skia's raster backend given one scene produces the same bytes
on every machine. A window golden is a function of the Mesa version the distribution ships, of which swapchain
format the surface offered, and of a GPU rasteriser whose antialiasing coverage is not the raster backend's. The
Arch development machine and the Ubuntu 24.04 runner do not ship the same Mesa, and pinning a byte would pin the
driver rather than the renderer.

Two thresholds, doing two different jobs:

| Threshold | Value | What it catches |
| --- | --- | --- |
| `MAX_CHANNEL_DELTA` | 8 of 255 | What counts as a differing pixel at all. A wrong colour, a swizzled channel order, a black frame or a stale buffer moves whole channels, not three percent of one. |
| `MAX_DIFFERING_PIXEL_FRACTION` | 1% | How much of the image may differ. 1% of 800x600 is 4800 pixels: more than the antialiased perimeter of every shape in both fixtures together, and far less than the smallest shape in them, so a moved, missing or unpainted element still fails. |

The division of labour is deliberate. Geometry to the pixel is the raster rig's job, and it already asserts it with
zero tolerance on the same two bundles. This rig's job is that the same scene survives the swapchain path at all.

### The fixtures and the workflow

`packages/core/test-bundles/fabric-view.js` and `view-props.js`, the same two bundles the raster rig uses, to
`packages/core/goldens/window-fabric-view.png` and `window-view-props.png`. They are separate files from the raster
goldens on purpose: the pictures describe the same scene but not the same bytes, and a shared file would force one
of the two rigs to accept the other's rasteriser. `text.js` is not a window fixture yet — the GPU glyph atlas is a
third rasterisation path and deserves its own thresholds rather than a share of these.

The comparison lives in `golden.spec.ts`, which runs the rig once at collection time — one compositor for all
fixtures, because starting one per image would cost more than the renders do — and skips the whole block with the
rig's own message when it exits 2.

```bash
cmake --build build/dev --target rnl_window
pnpm test:golden:window:update   # renders straight into packages/core/goldens
pnpm test:golden:window          # compares; must pass immediately afterwards
```

The review rule from *Golden images* applies unchanged: a regenerated golden is read as code, and one updated
because the test failed is a deleted test. When a window golden is missing the spec fails and names
`pnpm test:golden:window:update` rather than creating a baseline.

### The CI job

`window` is its own job on `ubuntu-24.04`, not a fourth entry in the `native` matrix. The reason is that
`native (dev)` proves the opposite property — that a configure with no Wayland and no Vulkan loader prints
`rnl_window is disabled, missing: ...` and carries on — so installing the graphics stack there would delete a
test. On top of the native apt list it adds `libwayland-dev` and `wayland-protocols` for the xdg-shell client,
`libxkbcommon-dev` for the keymap, `libvulkan-dev` for the loader and headers, `mesa-vulkan-drivers` for the
lavapipe ICD, and `weston`. It vendors React Native, Skia and the fonts from the same caches the native job uses,
builds `--target rnl_window` — which is also the assertion that the window half configured, since a skipped target
fails as an unknown target rather than silently — and runs `pnpm test:golden:window`.

That last step also greps its own output for `skipping window goldens` and fails if it finds it. The spec skips
when weston, lavapipe or the binary is missing, which is what keeps a laptop green and what would otherwise let an
incomplete apt list pass CI without rendering anything. As with the raster job, the assertion that the rig did not
skip is the assertion that it ran.

Its ccache entry is keyed `rnl-ccache-ubuntu-24.04-clang-18-window-...` and falls back through its own prefix to
the `dev` one, so a cold window job warms from the native job's cache; only the window sources are new. It saves
under its own key, so the two jobs never race for the same entry on a push to `main`. On failure the rendered PNG files
are uploaded as the `window-goldens` artifact, which is also how the first pair of goldens is produced on a machine
without weston: let the job fail on the missing goldens, download the artifact, review it, commit it.

## Unit tests and coverage

`packages/core/tests` is a second, Hermes-free CMake configure of the same source tree, reached only through the
`test` preset. `RNL_BUILD_TESTS` makes `packages/core/CMakeLists.txt` build ReactCommon's `jsi` directly instead of
taking the Hermes branch, add `packages/core/tests`, and `return()` before any Hermes-linked target — see hazard 3
above for why Hermes and GoogleTest cannot share a configure. The configure still needs the vendored React Native
tree (`ReactCommon` and `ReactCxxPlatform`) and the same folly/boost/glog/double-conversion/fmt prerequisites as
every other preset, because `RetainedScene` and `LinuxMountingManager` are ordinary renderer-core consumers; it
needs neither Hermes nor Skia, so it skips both the multi-hour Hermes build and the freetype/fontconfig/Wayland/
Vulkan prerequisites `RNL_ENABLE_SKIA` and `RNL_ENABLE_WINDOW` probe for.

`packages/core/tests/CMakeLists.txt` fetches googletest at a pinned commit, builds `rnl_core_tests` from
`SceneTest.cpp`, `InputTest.cpp`, `FocusTest.cpp`, `ImeTest.cpp`, `ImageTest.cpp` and `ScrollTest.cpp` plus the
seven sources they exercise — `RetainedScene.cpp`, `LinuxMountingManager.cpp`, `InputPipeline.cpp`,
`FocusModel.cpp`, `ImageContent.cpp`, `ScrollPhysics.cpp` and `TextInputV3State.cpp`, compiled
directly into the test binary rather than linked from a Hermes-linked library — and registers them with
`gtest_discover_tests` so `ctest` finds every `TEST` individually. Under Clang, `rnl_core_tests` also gets
`-fprofile-instr-generate -fcoverage-mapping`, LLVM's source-based coverage instrumentation.

`scripts/cpp-coverage.ts` is the gate: it runs `rnl_core_tests` with `LLVM_PROFILE_FILE` pointed at
`build/test/coverage`, merges the raw profile with `llvm-profdata merge -sparse`, exports it as lcov with
`llvm-cov export --format=lcov`, and grades line and branch coverage per file against an explicit list
(`scopedSourcePaths` in the script — today `RetainedScene.cpp`, `LinuxMountingManager.cpp`, `InputPipeline.cpp`,
`FocusModel.cpp`, `ImageContent.cpp`, `ScrollPhysics.cpp` and `TextInputV3State.cpp`, the seven sources the test
binary actually exercises). A source with no tests behind it is
deliberately not in that list: adding a file there without coverage behind it is what turns the gate red, rather
than a silent average across the whole of `packages/core`.

`InputDispatcher.cpp`, `WaylandSeat.cpp` and `TextInputClient.cpp` are the input sources deliberately left outside
that scope, and the split between them and `InputPipeline.cpp`, `FocusModel.cpp` and `TextInputV3State.cpp` is
what makes the scope honest rather than convenient: everything that
can be arithmetically wrong — motion coalescing, the queue bound, the buttons bitmask, the press-to-click state
machine, offset points, modifier flags, the key-name and key-code tables, the traversal order and its wraps, and
the `done` batching, ordering and serial rules of `zwp_text_input_v3` — lives in those three where the gate sees
it. What is left in
`InputDispatcher.cpp` is a `UIManager` hit test, a shadow-tree walk and a switch over five emitter calls, what is
left in `WaylandSeat.cpp` is protocol plumbing that needs a compositor, and what is left in `TextInputClient.cpp`
is requests and listeners that need one too. `--inject-pointer`, `--focus-tab` and `--ime-debug` are the tests for
those three.

`ImageContent.cpp` is inside it, and the split between it and `ImagePipeline.cpp` is what makes the image scope
honest: everything that can be arithmetically wrong — base64 decoding, source-scheme resolution, the five
`resizeMode` placements, the LRU order and the byte-bounded eviction — lives in `ImageContent.cpp` where the gate
sees it, and what is left in `ImagePipeline.cpp` and `ImageManager.cpp` is a Skia codec call, a worker thread and
upstream's request type. Those two are also outside the `test` configure entirely, because the ImageManager swap
is conditional on Skia; the image golden is what tests them. The scene half of images is inside the gate:
extraction from `ImageState`, the `resizeMode` mapping, the opacity fold into the tint and the decode-completion
damage all live in `RetainedScene.cpp` and `LinuxMountingManager.cpp` and are covered by `RetainedSceneImageTest`.

`ScrollPhysics.cpp` is inside it and `ScrollController.cpp` is outside it, and that split is the same one again:
the deceleration integral, the wheel impulse, the clamp, the stop threshold and the drag velocity are pure
arithmetic over doubles with no React types in them at all, and what is left in the controller is a `UIManager`
hit test, an ancestor walk, a state update and three emitter calls — none of which exists without a committed
shadow tree. `--scroll-to` is the test for that half. The scene half of scrolling is inside the gate: the
`contentOffset` read off `ScrollViewState`, the translation of the children, the unconditional clip and the damage
all live in `RetainedScene.cpp` and are covered by `RetainedSceneScrollTest`.

`TextPipeline.cpp` and `TextLayoutManager.cpp` are outside that scope for the same reason, and one stronger one:
they are the only two sources that are not even compiled in the `test` configure, because the stub swap is
conditional on Skia. Their correctness is visible as layout — a wrong measurement moves every line in the picture
— so the text golden is what tests them, and `packages/core/tests` never links Skia. The scene half of text is
inside the gate: extraction from `ParagraphState`, the opacity fold into fragment colours, the visibility rule and
the damage all live in `RetainedScene.cpp` and are covered by `RetainedSceneTextTest`.

`ScenePainter.cpp` is deliberately **not** in that scope. It could be: a `SkSurfaces::Raster` surface needs no GPU,
no Vulkan driver and no compositor, exactly as `hello_react --golden` proves. The cost is what rules it out — the
`unit` CI job today needs neither Hermes nor Skia and so skips both the vendored 93 MB Skia archive and the
freetype/fontconfig prerequisites, and linking `libskia.a` into `rnl_core_tests` would put all of that back into
the one job that is currently cheap. The answer instead is the split described in *View props fidelity*: every
number that could be wrong is computed in `RetainedScene.cpp`, where the gate does see it, and what is left in the
painter is Skia geometry that a golden image is a better test of than an assertion on a pixel would be. If the
painter ever grows logic that is not a draw call, that logic belongs in the scene, not in a new coverage entry.

Damage tracking tests that rule rather than breaking it. Every rectangle is computed in `RetainedScene.cpp` — the
transform mapping, the clip intersection, the subtree union, the merge policy — and what `clipToDamage` adds in the
painter is `SkRect::roundOut` plus a one-pixel outset, which is Skia's own pixel-coverage rule and cannot be
expressed in a Skia-free translation unit. The proof it is right is `--damage-golden`, which fails on a single
differing pixel; that is a stronger statement about pixel rounding than a line-coverage percentage would be.

The gate honors `// COV_EXCL: <reason>` markers per AGENTS.md — a marked line is dropped from both
the line and the branch count, and a marker with no reason after the colon fails the script outright. `LLVM_COV`
and `LLVM_PROFDATA` name the binaries to run and default to unversioned `llvm-cov`/`llvm-profdata`, which is what a
recent Arch `llvm` package puts on `PATH`; Ubuntu's `llvm-18` package installs only the versioned
`llvm-cov-18`/`llvm-profdata-18`, so CI sets both env vars.

```bash
cmake --preset test
cmake --build --preset test
ctest --preset test
node scripts/cpp-coverage.ts
```

or, from the root, the single script that chains all four steps:

```bash
pnpm test:native
```

`ctest --preset test` and `scripts/cpp-coverage.ts` both run every `TEST` in `rnl_core_tests`: the former for
per-test pass/fail reporting through CTest, the latter to produce the coverage-instrumented run the gate grades.
Both runs are deterministic and side-effect-free, so running the suite twice costs time, not correctness.

## Continuous integration

`.github/workflows/ci.yml` runs on every pull request and on every push to `main`, under
`permissions: contents: read` and a `cancel-in-progress` concurrency group. Every action is pinned by commit SHA
with the version in a trailing comment; Renovate keeps those SHAs fresh through `helpers:pinGitHubActionDigests`.

| Job | Runner | Timeout | What it proves |
| --- | --- | --- | --- |
| `validate` | `ubuntu-24.04` | 15 min | `pnpm validate`: format, types, lint, deadcode, duplication, Vitest with its 100% coverage thresholds, meta files. |
| `meta` | `ubuntu-24.04` | 10 min | actionlint, typos, shellcheck, shfmt, gitleaks. |
| `unit` | `ubuntu-24.04` | 30 min | `rnl_core_tests`, the Hermes-free GoogleTest suite for `RetainedScene` and `LinuxMountingManager`, run under `ctest` and gated at 100% line and branch coverage by `scripts/cpp-coverage.ts`. Needs neither Hermes nor Skia. |
| `native (dev)` | `ubuntu-24.04` | 120 min | The whole C++ toolchain: vendor, configure, build, the four `hello_react` acceptance paths, and the golden-image comparison. |
| `native (asan)` | `ubuntu-24.04` | 120 min | The same build and the same four paths under ASan + UBSan. |
| `native (tsan)` | `ubuntu-24.04` | 120 min | The same build and the same four paths under TSan. |
| `window` | `ubuntu-24.04` | 120 min | `rnl_window` built and run under `weston --backend=headless` with lavapipe, and the window goldens compared. The only job that reaches the Vulkan swapchain. See *Window goldens*. |

The three `native` entries are one matrix job with `fail-fast: false`, so a sanitizer failure never hides the
headless result. They are ordinary pull-request jobs rather than a push-to-main or label-gated workflow: they run
on separate runners, so the matrix costs wall clock equal to its slowest entry rather than the sum, and issue #4
requires the sanitizers to be merge-blocking. If the wall clock ever stops being tolerable, the exit is to move
the two sanitizer entries into their own workflow on `push: main` plus a `pull_request` label opt-in, and to say
so here.

### What the native job actually does

Vendoring is the three pinned scripts, unchanged from local use: `node scripts/vendor-react-native.ts` always,
and `node scripts/vendor-skia.ts` plus `node scripts/vendor-fonts.ts` only for the `dev` entry. The sanitizer entries configure with
`-DRNL_ENABLE_SKIA=OFF`, which drops the painter, the window and the golden path in one flag. That is deliberate:
the pinned Skia archive is an uninstrumented release build, so linking it into a sanitized binary buys shadow-memory
noise and no coverage.

`RNL_ENABLE_WINDOW` is left at its default `ON` even though no runner has Wayland or a Vulkan loader. Configure has
to print `rnl_window is disabled, missing: ...` and continue; CI is where that graceful-degradation path is proved,
so turning the option off explicitly would delete the test.

Only `--target hello_react` is built. Building `all` would additionally build Hermes' CLI tool suite — `hermes`,
`hvm`, `hbcdump` and the rest — none of which anything here runs. `hermesc` is still built, because
`InternalBytecode` depends on it.

The acceptance step is the documented checklist turned into assertions, and it runs identically in all three
entries:

1. `hello_react` with no argument prints `react-native-linux: hermes alive`.
2. `hello_react packages/core/test-bundles/hello.js` exits 0 and prints the timer, nested-timer and done lines, so
   the `PlatformTimerRegistry` feeding `TimerManager` is proved, not just bundle evaluation.
3. `hello_react packages/core/test-bundles/throws.js` exits **1** and prints a `[js-error] fatal` block.
4. `hello_react --fabric packages/core/test-bundles/fabric-view.js` prints the committed-surface line and the exact
   `View #4 frame=(24.00, 24.00, 120.00, 80.00) backgroundColor=rgba(51, 102, 204, 1)` line, so the Yoga output and
   the flattened-parent origin are pinned, not merely the fact that a commit happened.

A fifth acceptance step runs on the `dev` entry alone: `hello_react --fabric packages/core/test-bundles/text.js`
must print a `Paragraph` line carrying the heading's text, which proves the descriptors are registered and that
`ParagraphState` reached the scene. It is `dev`-only because the sanitizer entries build the upstream stub, where
every paragraph measures as zero.

`pnpm test:golden` then runs on the `dev` entry only. It is not a smoke test: `golden.spec.ts` skips itself when the
binary is missing, so the assertion that it does *not* skip is what proves the Skia half configured at all, and the
comparison is exact-equality against the checked-in PNG.

### Sanitizer policy

The sanitizer options are set as job environment and are deliberately loud: `detect_leaks=1` for ASan,
`halt_on_error=1` with `print_stacktrace=1` for UBSan, `halt_on_error=1` for TSan, each pointed at
`/usr/lib/llvm-18/bin/llvm-symbolizer` so frames are symbolized. There is no suppression file anywhere in this
repository, and per AGENTS.md there must never be one without an issue link in it. A leak or a race found by these
jobs is a bug to fix or an issue to file, never a line to add to a suppression list.

`vm.mmap_rnd_bits` is lowered to 28 before anything else runs. The sanitizer runtimes map fixed shadow regions and
abort with `unexpected memory mapping` when the kernel hands out more ASLR entropy than compiler-rt was built for;
28 is Ubuntu 24.04's own default, so the step is a no-op on a healthy runner and insurance on a changed one.

### Caches, and what is deliberately not cached

| Cache | Path | Key | Size |
| --- | --- | --- | --- |
| Vendored React Native | `third_party/react-native` | `rnl-vendor-react-native-ubuntu-24.04-<hash of scripts/vendor.lock.json + scripts/vendor-react-native.ts>` | ~18 MB |
| Vendored Skia | `third_party/skia` | `rnl-vendor-skia-ubuntu-24.04-<hash of scripts/skia.lock.json + scripts/vendor-skia.ts>` | ~93 MB |
| Vendored fonts | `packages/core/fonts` | `rnl-vendor-fonts-ubuntu-24.04-<hash of scripts/fonts.lock.json + scripts/vendor-fonts.ts>` | ~2 MB |
| ccache | `.ccache` | `rnl-ccache-ubuntu-24.04-clang-18-<preset>-<hash of the CMake files and both lock files>-<run id>` | 2 GB ceiling per preset |
| pnpm store | handled by `actions/setup-node` | `pnpm-lock.yaml` | small |

The three vendor caches are keyed on the lock file **and** the script that reads it, because a change to either
can change the tree. Both keys are content-deterministic, so the plain `actions/cache` action is used and a miss saves
automatically; both vendor scripts are no-ops on a hit, since they compare the restored `.vendor-stamp.json`
against the lock.

ccache is split: `actions/cache/restore` on every event, `actions/cache/save` only on a push to `main`. A per-preset
ccache is up to 2 GB and the repository-wide GitHub cache ceiling is 10 GB, so letting every pull request save its
own copy would evict the warm entry every other pull request reads. Pull requests therefore warm from `main` and
never pay the storage. The primary key carries the run id so it never hits, which is what makes the save
unconditional on `main`; the two `restore-keys` prefixes fall back first to the same toolchain inputs and then to
the preset alone. `max_size = 2G` and `compression = true` are written into `ccache.conf` before every build, so a
restored config cannot drift.

`build/<preset>/_deps` is **not** cached, and that is a decision rather than an omission. Installing `libboost-dev`
and `libgoogle-glog-dev` removes the two expensive FetchContent entries outright — Boost alone was a 145 MB download
expanding to 888 MB — and what remains is a shallow Hermes clone plus the folly and fast_float tarballs, roughly two
minutes of network. Caching that would cost ~300 MB of the 10 GB ceiling per preset and compete directly with
ccache, which is worth an order of magnitude more, and FetchContent's stamp reuse across a restored tree is fragile
in a way ccache's is not. Compiling those dependencies, which is the part that actually costs time, is covered by
ccache.

Cold start works with every cache empty; that is the 120-minute budget. A cold `native` entry is roughly 60–90
minutes, almost all of it Hermes plus the ReactCommon closure. With a warm ccache from `main` and both vendor trees
restored, expect 10–20 minutes, dominated by linking and by apt. Any pull request that edits `CMakePresets.json`,
either `CMakeLists.txt` or either lock file falls back to the preset-only restore key and pays a partial recompile;
one that changes the React Native pin pays a full one.

Disk is managed, not assumed. `build/dev` is about 4.5 GB and a sanitized build is larger, so the job first removes
the runner's unused Android, GHC and CodeQL toolchains — about 25 GB — and reports `df -h /` before the build and
`df -h /` plus `du -sh build/<preset>` after it, so a slow slide toward exhaustion shows up in the log before it
shows up as a mystery failure.

### Not wired up yet

Issue #4's `llvm-cov` C++ coverage gate is wired now; see *Unit tests and coverage* below. The lavapipe window
golden is wired now too; see *Window goldens*. The gcc column of the matrix is the remaining deferred piece, along
with the e2e driver's virtual Wayland input, which will run on the same compositor rig this job starts.

## Dependencies, and how they differ from Meta's

`private/react-native-fantom/tester/CMakeLists.txt` is the canonical template for a C++ React Native host. It pulls
`boost`, `glog`, `double-conversion`, `fast_float`, `fmt`, `folly`, `gflags`, `nlohmann_json` and OpenSSL, all
downloaded into an NDK staging directory by Gradle's `prepareNative3pDependencies`. This build needs a subset,
because it stops at the `ReactInstance` rather than the full fantom host:

| Dependency | Here | Why |
| --- | --- | --- |
| boost | system headers, else classic source archive at 1.83.0 | React Native's own boost target is include-only and its Gradle task extracts only `boost/**/*.hpp`, so no compiled Boost library is ever linked. Hermes vendors its own Boost.Context separately. |
| glog | system, else built from source at 0.7.1 | RN's 0.3.5 pin has no standalone CMake build — Gradle generates its `config.h` and substitutes tokens into the `.h.in` headers. glog 0.7.1's own CMake defines the target name ReactCommon links, `glog`, so it drops straight in with `WITH_GFLAGS=OFF`, `WITH_GTEST=OFF`, `WITH_UNWIND=none`. |
| double-conversion, fmt | system | Distribution versions satisfy folly and ReactCommon. |
| fast_float | FetchContent at RN's pin | No Ubuntu package. |
| folly | FetchContent at RN's pin, RN's subset source list | ReactCommon compiles every TU with `-DFOLLY_NO_CONFIG=1`, which is ABI-incompatible with a distribution folly built against `folly-config.h`. |
| gflags | not used | Only fantom's own CLI needs it. |
| nlohmann_json, OpenSSL | not used | Only `ReactCxxPlatform`'s HTTP/WebSocket clients need them, and only `react/threading` is linked from that tree. They arrive with the Metro dev server and the inspector. |

Two flags fantom sets are deliberately dropped, because fantom targets the NDK and libc++ while this targets glibc
and libstdc++: `FOLLY_USE_LIBCPP` (folly would include libc++'s `<__config>`) and `FOLLY_HAVE_XSI_STRERROR_R`
(glibc's `strerror_r` is the GNU variant and returns `char*`).

## Known hazards

1. **`HERMES_ENABLE_TOOLS=ON` is mandatory** even though no CLI is shipped. Hermes precompiles `InternalBytecode`
   with its own `hermesc` during the build and Ninja fails with a missing-rule error when tools are off.
2. **libstdc++ include hygiene.** Hermes' CDP and llvh sources assume `<string>`, `<memory>`, `<vector>`,
   `<cstdint>`, `<cstring>` arrive transitively, which stopped being true with libstdc++ 13. The build force-includes
   them for the duration of Hermes' `add_subdirectory` and restores `CMAKE_CXX_FLAGS` afterwards. Upstream has no
   Linux CI job with `HERMES_ENABLE_DEBUGGER=ON`, so this keeps recurring; adding one is the cheapest useful
   upstream contribution this project can make.
3. **gtest collides with Hermes' llvh.** `external/llvh/CMakeLists.txt` adds `utils/unittest` unconditionally, which
   defines the `gtest` and `gtest_main` target names. `HERMES_ENABLE_TEST_SUITE=OFF` does not prevent that — the
   option gates Hermes' own test suite, not llvh's unconditional `add_subdirectory` — so an upstream googletest
   `FetchContent` in the same configure as Hermes fails with "target gtest already defined" at Hermes'
   `add_subdirectory`, regardless of fetch order. The resolution is the Hermes-free `test` CMake preset:
   `RNL_BUILD_TESTS` builds ReactCommon's `jsi` directly instead of taking the Hermes branch, so Hermes and its
   vendored llvh are never added to that configure and GoogleTest owns the `gtest` name. See *Unit tests and
   coverage*.
4. **`jsi` is defined twice upstream.** Hermes builds a `jsi` target and so does ReactCommon. `JSI_DIR` points
   Hermes at React Native's `ReactCommon/jsi`, so exactly one `jsi` target exists; the build then appends
   `JSIDynamic.cpp` and `jsilib-posix.cpp`, which Hermes' own CMakeLists does not compile but ReactCommon needs.
   `HERMES_BUILD_SHARED_JSI=ON` keeps that single `jsi` a shared library so `hermesvm` and the executable resolve
   the same definition.
5. **ReactCommon compiles with `-Werror`.** Every ReactCommon target here gets `-Wno-error` appended, rather than
   carrying a patch for each distribution compiler that introduces a new warning.
6. **CMake 4 and old `cmake_minimum_required`.** Hermes' vendored Boost.Context declares `VERSION 3.5...3.16`.
   If a future CMake refuses it, configure with `-DCMAKE_POLICY_VERSION_MINIMUM=3.5`.

## First-build fix ledger (2026-09-01, clang 22, Arch)

1. ReactCommon subdirectory CMakeLists assume the parent includes `cmake-utils/react-native-flags.cmake` and `cmake-utils/internal/react-native-platform-selector.cmake`; both are now included after `REACT_COMMON_DIR` is set.
2. folly `Demangle.cpp` fails against binutils >= 2.44 `demangle.h` (`demangle_callbackref` removed); the FetchContent `PATCH_COMMAND` forces folly's supported no-libiberty path.
3. `jsinspector-modern/network/HttpUtils.h` misses `<cstdint>` under libstdc++ 16; a directory-wide `-include cstdint` covers the class of problem.
4. `HERMES_ENABLE_RTTI=ON` is required: llvh otherwise builds without RTTI and every RTTI-enabled consumer of `libhermesvm.so` fails on missing `llvh::cl::*` typeinfo.
5. `JSIDynamic.cpp`/`jsilib-posix.cpp` must not be appended to Hermes' `jsi` target — its `-fvisibility=hidden` strips their unannotated symbols; they live in the `rnl_jsidynamic` static library instead.
6. `hello_react` links `atomic`: `std::atomic<std::optional<double>>` in `ReactNativeFeatureFlagsAccessor` needs `__atomic_load_16`/`__atomic_store_16` on x86-64 glibc.

## Window fix ledger (2026-09-01, clang 22, Arch)

1. `add_compile_options(-include cstdint)`, added for hazard 3 above, is now wrapped in `$<COMPILE_LANGUAGE:CXX>`. It applied to the whole directory, and `wayland-scanner`'s generated `xdg-shell-protocol.c` is C, which cannot include a C++ header. The `SHELL:` prefix keeps the two tokens together.
2. **`SK_RELEASE` must be defined even in the Debug preset.** Skia's public headers change class layout under `SK_DEBUG`, and `SkTypes.h` infers `SK_DEBUG` from the absence of `NDEBUG`. Against a release prebuilt that is an ABI mismatch that links and then misbehaves. `SK_GANESH` and `SK_VULKAN` are set for the same parity reason.
3. Skia is reached as `#include "include/core/..."` with `third_party/skia` itself on the include path, which is how Skia expects to be included and why the header tree is vendored at its repository root rather than flattened.
4. `<vulkan/vulkan.h>` is deliberately not used. It only includes `vulkan_wayland.h` behind `VK_USE_PLATFORM_WAYLAND_KHR`, and a `#define` that must precede an include does not survive `clang-format`'s `IncludeBlocks: Regroup`. `vulkan_wayland.h` has no guard of its own, so it is included directly.
5. Skia's `tools/window/VulkanWindowContext.cpp` is the reference for the frame path but cannot be linked: it includes `src/` private headers that no prebuilt ships. The structure is copied, the code is not.
6. `VkSurfaceCapabilitiesKHR::currentExtent` is `0xFFFFFFFF` on Wayland, so the swapchain extent comes from the last `xdg_toplevel.configure` clamped to the reported min and max, never from the surface.
7. `wl_display_dispatch` cannot be used. The Vulkan WSI dispatches the same connection on its own private event queue, so the run loop uses the `wl_display_prepare_read`/`wl_display_read_events` protocol with `poll` in between.
8. **A Skia flush with no recorded drawing signals no semaphores, and the window deadlocks on the next acquire.** Found with `rnl_window --screenshot --frames 30` on hardware: five frames worked, thirty hung with no output. Once every swapchain image had painted the settled scene, every later frame was idle, and the idle path flushed Skia with a signal semaphore and no work. `GrDirectContext::flush` documents the outcome — it returns `GrSemaphoresSubmitted::kNo` and "the client should not have the GPU wait on any of the semaphores passed in" — but `vkQueuePresentKHR` was waiting on that render semaphore regardless, so the present never completed, the image was never released, and the next `vkAcquireNextImageKHR(UINT64_MAX)` blocked forever. The fix is an explicit empty `vkQueueSubmit` on the idle path instead of the flush; see *Damage tracking*. The general lesson is that `flush`'s return value is a contract, not a status code to discard.
