# C++ toolchain

`packages/core` builds `hello_react`: a bridgeless `ReactInstance` running a JavaScript bundle on Hermes, linked
against the React Native renderer core and Yoga. It is the toolchain proof for issue #3, the engine-embedding proof
for issue #9, and the headless half of the Fabric bootstrap for issue #10. It is not a renderer: nothing here draws.

`packages/core` also builds `rnl_window`: an xdg-shell window on Wayland, a FIFO Vulkan swapchain, and a Skia Ganesh
`GrDirectContext` on that device, driven by `wl_surface` frame callbacks. That is issue #8. With
`--fabric <bundle>` it boots the same `ReactInstance` and `FabricHost` inside the window process and draws the
retained scene every frame — issue #32, the first React-driven pixels. Without the flag it draws the static
placeholder card and runs no JavaScript.

The engine half is shared rather than duplicated: `rnl_react_core` is a static library carrying the instance, the
Fabric host and the retained scene, and both executables link it. `hello_react` adds the headless runner,
`rnl_window` adds Wayland, Vulkan and Skia.

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
frame itself is Yoga output. The dump is ordered by mount order under sorted root tags so it can become a golden
fixture in issue #6.

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
  one rounded rectangle in `#3366CC`, inset 64 px with a 24 px corner radius. With one it clears to the same
  background and fills one `SkRect` per painted scene node.

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
  loop — and one packed ARGB colour per node that has a meaningful `backgroundColor`. Nodes without one, including
  the surface root, contribute nothing, because the background clear already covers them.
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

Not drawn yet, each with an owning milestone: text (#14), borders, radii, opacity and transforms, `overflow`
clipping, and `display: none`. Events are M1/M2 — the `EventBeat` is still never induced, so the window is a
display, not an application.

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

```bash
sudo apt-get install -y build-essential git cmake ninja-build ccache python3 \
  libboost-dev libdouble-conversion-dev libfmt-dev libgoogle-glog-dev libicu-dev
```

Two version caveats on Ubuntu 24.04, both understood and accepted:

- `libfmt-dev` is 10.x while React Native pins fmt 12.1.0. folly's subset only needs fmt >= 8.
- `cmake` is 3.28.3, exactly the minimum this build declares.

`fast_float` has no Ubuntu package, so it is fetched at React Native's pinned version rather than split across
two acquisition paths.

## Commands

```bash
pnpm --filter @react-native-linux/core vendor       # node scripts/vendor-react-native.ts
pnpm --filter @react-native-linux/core vendor:skia  # node scripts/vendor-skia.ts
pnpm --filter @react-native-linux/core configure    # cmake -S <repo root> --preset dev
pnpm --filter @react-native-linux/core build        # cmake --build build/dev
pnpm --filter @react-native-linux/core run:hello    # build/dev/bin/hello_react
pnpm --filter @react-native-linux/core run:fabric   # build/dev/bin/hello_react --fabric <bundle>
pnpm --filter @react-native-linux/core run:window   # build/dev/bin/rnl_window
pnpm --filter @react-native-linux/core run:window:fabric  # build/dev/bin/rnl_window --fabric <bundle>
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
retained scene after the JavaScript thread goes quiet; see *Fabric bootstrap* above.

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

`RNL_ENABLE_WINDOW` defaults to `ON`. When it is on but a dependency is absent — no Skia in `third_party`, no
`wayland-scanner`, no Vulkan loader, no freetype or fontconfig — configure prints
`rnl_window is disabled, missing: ...` and skips the target rather than failing, so the headless build survives on a
machine that has none of it. `cmake --preset dev -DRNL_ENABLE_WINDOW=OFF` skips it deliberately and silently.

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
   picture through lavapipe. That is the path the headless golden-image rig will use.
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

Not covered by this checklist, because it is not implemented yet: fractional scale, pointer and keyboard input,
`wp_presentation_feedback` timing, and any measurement of the frame budget.

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
3. **gtest collides with Hermes' llvh.** Hermes' bundled llvh ships its own googletest. `HERMES_ENABLE_TEST_SUITE`
   is forced off here, but wiring a GoogleTest target for `packages/core` in the same configure as Hermes still has
   to solve the collision — either by keeping the test binary in a Hermes-free configure, or by fetching googletest
   before Hermes and letting the existing targets win.
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
