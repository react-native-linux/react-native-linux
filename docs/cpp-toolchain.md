# C++ toolchain

`packages/core` builds `hello_react`: a bridgeless `ReactInstance` running a JavaScript bundle on Hermes, linked
against the React Native renderer core and Yoga. It is the toolchain proof for issue #3, the engine-embedding proof
for issue #9, and the headless half of the Fabric bootstrap for issue #10. It is not a renderer: nothing here draws.

`packages/core` also builds `rnl_window`: an xdg-shell window on Wayland, a FIFO Vulkan swapchain, and a Skia Ganesh
`GrDirectContext` on that device, driven by `wl_surface` frame callbacks. That is issue #8. With
`--fabric <bundle>` it boots the same `ReactInstance` and `FabricHost` inside the window process and draws the
retained scene every frame — issue #32, the first React-driven pixels. Without the flag it draws the static
placeholder card and runs no JavaScript.

`hello_react --golden <bundle> <output.png>` is the third mode and the producer half of the golden-image rig from
issue #6: it boots the same headless Fabric host, renders the settled scene into an **offscreen raster**
`SkSurface` and writes a PNG. No GPU, no Vulkan driver, no Wayland compositor. See *Golden images*.

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
  executor, unbuffered bindings executor, a base `EventBeat`, and a component registry with the `RootView` and
  `View` descriptors), constructs a `Scheduler`, and starts one `SurfaceHandler` with an 800x600 layout
  constraint. Constructing the `Scheduler` is what installs `nativeFabricUIManager` into the runtime.
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

Not covered yet, each with an owning milestone: events (the `EventBeat` is never induced, so nothing can be
dispatched), `Scheduler::reportMount` and mount-hook telemetry, `dispatchCommand`, multiple surfaces, and every
component past `View`.

## Window host

`rnl_window` is four files under `packages/core/src`, in the order the frame flows through them:

- `WaylandWindow` owns one connection, one `wl_surface`, one `xdg_toplevel` titled `react-native-linux` at 800x600,
  and the run loop. It never attaches a buffer; `vkQueuePresentKHR` does that. `xdg_toplevel.close` sets the exit
  flag, `xdg_toplevel.configure` records a resize, and `xdg_wm_base.ping` is answered.
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

- `snapshotScene`, once per frame, which copies the scene out under `LinuxMountingManager`'s mutex. The copy carries
  absolute frames — parent-relative Fabric frames are accumulated during the walk, in the scene, not in the paint
  loop — plus the absolute transform, the inherited `overflow: hidden` clips, the resolved border metrics, and
  packed ARGB colours with the inherited opacity already multiplied in. See *View props fidelity*. Nodes with
  neither a visible background nor a visible border, including the surface root, contribute nothing, because the
  background clear already covers them.
- `resize`, on `xdg_toplevel.configure`, which calls `SurfaceHandler::constraintLayout` with the new size as both
  the minimum and the maximum. That commit uses the default commit options, whose `mountSynchronously` is true, so
  the Yoga relayout **and** the resulting mount run on the frame thread. Thread affinity is therefore not what keeps
  the scene consistent — the mutex is, and it is the only thing that is.

A snapshot is a full copy and every frame is a full repaint. At one `View` that is cheaper than any alternative, and
damage tracking is issue #12, not this one.

Shutdown is stop, drain, destroy, in that order: `~WindowSession` stops the surface, drains the JavaScript thread so
the queued unmount runs while the scheduler delegate is still alive, and then lets the Fabric host and the instance
destruct. `xdg_toplevel.close` and a `wl_display` error both leave the run loop normally, so both take that path.
`Ctrl-C` does not: there is still no signal handler, so `SIGINT` terminates the process where it stands and the
compositor reclaims the surface when the connection's file descriptor closes.

Not drawn yet, each with an owning milestone: text (#14), shadows and elevation, `boxShadow`, `filter`,
`mixBlendMode`, `outline`, `backgroundImage` gradients, and `display: none`. Backgrounds, per-corner radii,
per-side borders, opacity, transforms and `overflow` clipping are drawn; see *View props fidelity*. Events are
M1/M2 — the `EventBeat` is still never induced, so the window is a display, not an application.

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
sudo pacman -S --needed wayland wayland-protocols vulkan-headers vulkan-icd-loader \
  freetype2 fontconfig
```

`hello_react --golden` needs only `freetype2` and `fontconfig` from that list, plus the vendored Skia archive. It
never opens a Wayland connection and never loads a Vulkan driver.

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
pnpm --filter @react-native-linux/core vendor       # node scripts/vendor-react-native.ts
pnpm --filter @react-native-linux/core vendor:skia  # node scripts/vendor-skia.ts
pnpm --filter @react-native-linux/core configure    # cmake -S <repo root> --preset dev
pnpm --filter @react-native-linux/core build        # cmake --build build/dev
pnpm --filter @react-native-linux/core run:hello    # build/dev/bin/hello_react
pnpm --filter @react-native-linux/core run:fabric   # build/dev/bin/hello_react --fabric <bundle>
pnpm --filter @react-native-linux/core run:golden   # hello_react --golden <bundle> /tmp/rnl-fabric-view.png
pnpm --filter @react-native-linux/core run:window   # build/dev/bin/rnl_window
pnpm --filter @react-native-linux/core run:window:fabric  # build/dev/bin/rnl_window --fabric <bundle>

pnpm test:golden          # compare renders against the checked-in goldens
pnpm test:golden:update   # regenerate the goldens, for review before committing
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
./build/dev/bin/rnl_window
./build/dev/bin/rnl_window --fabric packages/core/test-bundles/fabric-view.js
```

Without an argument the expected output is `react-native-linux: hermes alive`. With
`packages/core/test-bundles/hello.js` the bundle prints its own evaluation, microtask and timer lines and the
process exits 0. `packages/core/test-bundles/throws.js` is the error fixture: it prints a `[js-error] fatal` block
with a parsed stack to stderr and exits 1. `--fabric` takes the bundle path as its own argument and prints the
retained scene after the JavaScript thread goes quiet; see *Fabric bootstrap* above. `--golden` takes the bundle
path and an output path, prints nothing of its own, and writes a PNG; see *Golden images*. On a build configured
without Skia it exits 1 with a message naming `scripts/vendor-skia.ts`.

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
`build/<preset>/packages/core/wayland-protocols` and compiled into `rnl_xdg_shell`. Nothing generated is checked in.

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
   picture through lavapipe. That is the path the deferred full-window golden will use; the golden rig that exists
   today renders offscreen on the CPU and never reaches a driver. See *Golden images*.
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

Not covered by this checklist, because it is not implemented yet: fractional scale, pointer and keyboard input,
`wp_presentation_feedback` timing, and any measurement of the frame budget.

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

## Golden images

The rig has two halves. `hello_react --golden` produces a PNG; a Vitest spec compares it against a checked-in one.

```text
hello_react --golden <bundle> <output.png> [width height]
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
  space through the WSI, or `wl_surface` presentation. Every one of those is exactly what the lavapipe plus
  `weston --backend=headless` golden is for, and that rig is a follow-up issue, not this one.

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
here is exact equality. A tolerance would only hide a real regression. When the lavapipe window golden lands it
needs its own comparator with its own stated threshold, not a loosened version of this one.

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
`SceneTest.cpp` plus the two sources it exercises — `RetainedScene.cpp` and `LinuxMountingManager.cpp`, compiled
directly into the test binary rather than linked from a Hermes-linked library — and registers them with
`gtest_discover_tests` so `ctest` finds every `TEST` individually. Under Clang, `rnl_core_tests` also gets
`-fprofile-instr-generate -fcoverage-mapping`, LLVM's source-based coverage instrumentation.

`scripts/cpp-coverage.ts` is the gate: it runs `rnl_core_tests` with `LLVM_PROFILE_FILE` pointed at
`build/test/coverage`, merges the raw profile with `llvm-profdata merge -sparse`, exports it as lcov with
`llvm-cov export --format=lcov`, and grades line and branch coverage per file against an explicit list
(`scopedSourcePaths` in the script — today `RetainedScene.cpp` and `LinuxMountingManager.cpp`, the two sources
`SceneTest.cpp` actually exercises). A source with no tests behind it is deliberately not in that list: adding a
file there without coverage behind it is what turns the gate red, rather than a silent average across the whole of
`packages/core`.

`ScenePainter.cpp` is deliberately **not** in that scope. It could be: a `SkSurfaces::Raster` surface needs no GPU,
no Vulkan driver and no compositor, exactly as `hello_react --golden` proves. The cost is what rules it out — the
`unit` CI job today needs neither Hermes nor Skia and so skips both the vendored 93 MB Skia archive and the
freetype/fontconfig prerequisites, and linking `libskia.a` into `rnl_core_tests` would put all of that back into
the one job that is currently cheap. The answer instead is the split described in *View props fidelity*: every
number that could be wrong is computed in `RetainedScene.cpp`, where the gate does see it, and what is left in the
painter is Skia geometry that a golden image is a better test of than an assertion on a pixel would be. If the
painter ever grows logic that is not a draw call, that logic belongs in the scene, not in a new coverage entry. The gate honors `// COV_EXCL: <reason>` markers per AGENTS.md — a marked line is dropped from both
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

The three `native` entries are one matrix job with `fail-fast: false`, so a sanitizer failure never hides the
headless result. They are ordinary pull-request jobs rather than a push-to-main or label-gated workflow: they run
on separate runners, so the matrix costs wall clock equal to its slowest entry rather than the sum, and issue #4
requires the sanitizers to be merge-blocking. If the wall clock ever stops being tolerable, the exit is to move
the two sanitizer entries into their own workflow on `push: main` plus a `pull_request` label opt-in, and to say
so here.

### What the native job actually does

Vendoring is the two pinned scripts, unchanged from local use: `node scripts/vendor-react-native.ts` always, and
`node scripts/vendor-skia.ts` only for the `dev` entry. The sanitizer entries configure with
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
| ccache | `.ccache` | `rnl-ccache-ubuntu-24.04-clang-18-<preset>-<hash of the CMake files and both lock files>-<run id>` | 2 GB ceiling per preset |
| pnpm store | handled by `actions/setup-node` | `pnpm-lock.yaml` | small |

The two vendor caches are keyed on the lock file **and** the script that reads it, because a change to either can
change the tree. Both keys are content-deterministic, so the plain `actions/cache` action is used and a miss saves
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

Issue #4's `llvm-cov` C++ coverage gate is wired now; see *Unit tests and coverage* below. The gcc column of the
matrix, and the lavapipe window golden, are the remaining deferred pieces.

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
