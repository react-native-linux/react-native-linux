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

`hello_react --type <bundle> <out.png> "<sequence>"` is the fifth, and the proof for issue #17: it boots the same
headless Fabric host, presses Tab to land on the fixture's `<TextInput>`, types a key sequence written in a one-line
notation — literal characters, `{Left}`, `{Shift+Left}`, `{Ctrl+A}`, `{Backspace}`, `{Enter}` and the composition
tokens `{Preedit:...}` and `{Commit:...}` — and renders the result. The bundle prints the event trace and the PNG
shows the caret, the selection and the composing underline. See *TextInput*.

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
  `View`, `Image`, `ScrollView`, `Paragraph`, `Text`, `RawText` and `TextInput` descriptors), constructs a `Scheduler`, and starts one
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

Not covered yet, each with an owning milestone: `Scheduler::reportMount` and mount-hook telemetry, multiple
surfaces, and every component past `View`, `Paragraph`, `Image`, `ScrollView` and `TextInput`. Events are covered;
see *Input*. Scrolling is covered; see *ScrollView*. Text editing is covered; see *TextInput*.
`dispatchCommand` is ordered and delivered but not yet executed against a component; see *Commit termination and
mounting atomicity*.

## Commit termination and mounting atomicity (#125)

`ReactCommon`'s commit and mounting code is compiled into `rnl_window`, so its Fabric-era defects reach this
platform by linkage rather than by analogy. Three of them have a contract here, and
`packages/core/tests/ShadowTreeCommitTest.cpp` asserts them against a real `facebook::react::ShadowTree` and its
`MountingCoordinator` rather than against a scripted mutation list.

### The missing-tag policy

A mutation that updates, removes or deletes a tag `RetainedScene` does not hold — upstream's "Unable to find
viewState for tag N. Surface stopped: false", [core#49077][core-49077] — used to be a silent skip: `deleteNode`
erased nothing, `removeChild` unlinked nothing, and `updateNode` quietly conjured a node that no commit had
created. Silence is the bug. The policy is now loud but not fatal:

- `LinuxMountingManager::verifyTagIsKnown` asks `RetainedScene::hasNode` before an `Update`, a `Remove`, a
  `Delete` or a `dispatchCommand` is applied.
- Every miss increments `MountDiagnostics::unknownTagOperations`. The **first** one is kept whole — which
  operation, which tag, which transaction number — and logged once through `LOG(ERROR)`. Logging once is what
  keeps a systematically broken commit from turning the frame thread into a log writer.
- The operation is then applied anyway and the transaction runs to its end. The frame thread never throws and
  never aborts, because a wrong frame is recoverable and a dead frame thread is not.

`mountDiagnostics()` is the read side, and it is cumulative for the life of the surface: reading it does not clear
it. A well-formed transaction leaves it at zero, which is what the `ShadowTree`-driven tests assert after mounting
a real transaction — the strongest available statement that our mounting layer and upstream's differ agree about
which tags exist.

### The command ordering contract

`dispatchCommand` arrives on the JavaScript thread and names a node the frame thread owns, so executing it where
it arrives would race mounting — [core#47576][core-47576] with the sign flipped. Instead:

- A command is appended to a queue under the same mutex `executeMount` takes, in arrival order.
- The frame consumer calls `takeCommands()` right after `takeFrame()`. It drains the whole queue and empties it.
- Therefore a command queued after a transaction is observed after that transaction has been applied to the
  scene, and before any transaction that lands afterwards. `LinuxMountingManagerCommandTest` asserts exactly that
  with a sequence log built from what the consumer sees: `mounted 10x10`, `command scrollToEnd #2`,
  `mounted 20x20`.
- A command naming an unknown tag is recorded in the diagnostics and **still delivered**. Dropping it would be
  the same silence the missing-tag policy exists to remove.

Executing a command against a component is still out of scope; the ordering is the part that had to be decided
before any component could rely on it.

### What the ShadowTree tests prove

`ShadowTreeCommitTest.cpp` builds the real thing: a `ShadowTree` with a pass-through `ShadowTreeDelegate`, a
`ViewComponentDescriptor` for the children, and the `MountingCoordinator` the tree owns. It needs no Hermes, no
JSI runtime and no upstream test-utility tree — `react_renderer_mounting`, `rrc_root` and `rrc_view` are already
linked into `rnl_core_tests`, so the only build change is the source file itself.

- **Termination.** `ShadowTree::commit` retries `tryCommit` in a loop that, with the default feature flags, has no
  bound other than `react_native_assert(attempts < 1024)` — [core#51870][core-51870]. A transaction whose
  re-entrant state update moves the base tree once converges on the second attempt and reports `Succeeded`. One
  that moves it on every attempt is only bounded when `preventShadowTreeCommitExhaustion` is on, where the loop
  stops after three attempts, takes the recursive revision lock, tries once more, and reports `Failed` — four
  transaction calls, an outcome instead of a spin. That is the flag this platform wants on.
- **Atomicity.** Two commits before a single `pullTransaction` produce **one** `MountingTransaction` whose
  mutations describe only the final tree: the child the first commit rendered never appears in any mutation, so
  the intermediate tree is not observable through the coordinator at all. That is [core#44111][core-44111]'s
  visible intermediate frame, ruled out at the coordinator rather than papered over downstream.
- **Cross-thread serialisation.** Commits on one thread and `pullTransaction` on another —
  [core#52373][core-52373], which is this platform's default rather than an edge case — leave the scene holding
  exactly the last committed tree with zero unknown-tag operations, whatever the interleaving. The test is
  deterministic by construction: a `std::latch` starts both threads and a commit counter, not a sleep, ends the
  consumer loop.

### Follow-ups

- **TSan.** `ConcurrentPullTransactionIsSerializedAgainstCommitsOnAnotherThread` is written to run under the
  `tsan` preset; wiring it into the sanitizer job is separate work.
- **Draining commands in the window.** `takeCommands` has no production consumer yet: `FabricHost`,
  `WindowSession` and `WindowMain` still call only `takeFrame`. Nothing dispatches a command on this platform
  today, so the queue stays empty, but the first component that accepts one has to pull `takeCommands` through
  those three files in the same change.
- **E2E.** A frame-by-frame capture proving no intermediate frame reaches the compositor for a layout-effect
  update needs the harness, not a unit test.
- **Drift.** The code under test is vendored, so this file re-runs on every React Native bump as part of the
  upstream-parity oracle.

[core-44111]: https://github.com/facebook/react-native/issues/44111
[core-47576]: https://github.com/facebook/react-native/issues/47576
[core-49077]: https://github.com/facebook/react-native/issues/49077
[core-51870]: https://github.com/facebook/react-native/issues/51870
[core-52373]: https://github.com/facebook/react-native/issues/52373

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

### Frame clock (#59)

The fallback in *Pacing* keeps the connection dispatching, but on its own it says nothing about whether the frame
it wakes for should actually be drawn. Gating that naively on "did the callback fire" would make an occluded
window's animation stop dead the moment Hyprland stops sending callbacks for it; drawing unconditionally on every
wakeup, callback or fallback, makes a hidden window redraw every 50 ms forever for no reason. `FrameClock`
(`packages/core/src/FrameClock.h`) is the state machine that picks neither of those: a pure, dependency-free class
with no Wayland and no clock reads of its own — every timestamp is a `std::chrono::steady_clock::time_point` the
caller passes in.

Two inputs, one flag:

- `onFrameCallback(now)` always draws. `wl_surface.frame` firing means the compositor is presenting this surface,
  so there is never a reason to skip it.
- `onFallbackTimeout(now, hasPendingWork)` draws only if `hasPendingWork` is true. An idle occluded window with a
  static picture must not spin the GPU on every 50 ms wakeup; a window with something still moving must not stall
  just because nothing sent it a callback.

The delta each drawing tick reports is wall-clock time since the *last drawing tick*, regardless of which input
produced either one — a fallback-timeout draw is not measured from the last callback, and a skipped (no-work)
timeout does not move that reference at all, so the next real tick's delta is still correct. The first callback to
arrive after one or more fallback-driven ticks is flagged `resumed`, exactly once, which is what a caller would use
to detect "the frame source came back" rather than "vsync ticked again". Liveness is otherwise just counters:
`callbackTicks`, `timerTicks`, `resumeTransitions` and `lastCallbackAt` — a frame source that has gone silent shows
up as `timerTicks` climbing while `callbackTicks` stops. There is no Tracy integration yet; `WindowSession::frameClock()`
is a plain getter until one exists.

`WindowSession` owns one `FrameClock`, separate from the `lastFrameTime_` clock `deliverInput` already uses for
scroll physics and the caret blink — that clock paces *input*, once per loop iteration regardless of whether a
paint happens, and does not need to know about draw gating. `recordFrameTick` is the bridge: `WindowMain`'s run
loop calls it once per iteration with `Source::Callback` when `WaylandWindow::hasFrameCallbackFired()` is true (the
callback fired during the previous `waitForRedraw`) and `Source::Timer` otherwise, and only calls `takeFrame` plus
`SkiaVulkanRenderer::drawFrame` when the returned `Tick::shouldDraw` is true. Skipping the call is what skips the
GPU work — `drawFrame` is also what re-arms the next frame callback, so a skipped iteration correctly leaves the
previous callback (or lack of one) outstanding rather than requesting a new one it would just have to skip again.

`hasPendingWork` for a `Timer` tick is four signals, ORed, computed by `WindowSession` and `FabricHost` between
them:

- `LinuxMountingManager::hasPendingDamage()` — a mount, an image decode, a focus change or an editor-state publish
  since the last `takeFrame`. This is a dedicated flag on the mounting manager, not a peek at `RetainedScene`'s
  damage list, because the retained scene has no non-consuming way to ask "is there damage" without changing its
  mutation-tracking contract.
- `ScrollController::isScrollActive()` — any target still being dragged or still gliding, read from the controller's
  own state rather than by calling `advance` a second time for the same frame.
- `ReactHost::hasPendingTimers()` — any JS timer outstanding. `HostTimerRegistry` tracks a timer's delay and whether
  it recurs, not an absolute deadline, so this is "a timer exists" rather than "a timer is due before the next
  tick": a conservative signal that can hold the fallback awake a little ahead of when a distant `setTimeout`
  actually fires, never behind. A precise `hasDueTimer(now)` would need the registry to track absolute deadlines
  across the JS-thread/frame-thread boundary it does not track today, which is more machinery than this fix needs.
- `LinuxAnimationChoreographer::isActive()` — a running animation, which is the signal #129 added. `resume` and
  `pause` come from `AnimationBackend::start` and `::stop`, so this is exactly "the backend has a frame callback
  registered", and it is what stops an animation freezing the moment Hyprland withholds callbacks for a background
  workspace. See *Animation choreographer*.

What this does not cover: an end-to-end test under the headless compositor that actually withholds frame callbacks
and asserts the window keeps animating from the fallback alone is a follow-up, not part of this change. `FrameClock`
itself is unit-tested exhaustively (`packages/core/tests/FrameClockTest.cpp`); the withheld-callback path is
exercised today only by reasoning about `WaylandWindow` and `SkiaVulkanRenderer`, not by a running compositor.

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

Not drawn yet, each with an owning milestone: elevation, `filter`, `mixBlendMode`, `outline`, and
`display: none`. Backgrounds, per-corner radii, per-side borders, opacity, transforms, `overflow` clipping,
gradients and shadows are drawn; see *View props fidelity*. Text is drawn; see *Text*.
Images are drawn; see *Image*. Pointer and keyboard events reach JavaScript once per frame; see *Input*. The scene
is also what a press is hit-tested against, which is what keeps the pressed node and the painted node the same
node while an animation is running; see *Hit-testing under animation*.

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

That reconfigure is also the bump gate for *Upstream animated tests* below: `UpstreamAnimatedSuiteTest` fails the
build if the vendored `animated/tests/` file list moved, and `rnl_core_tests` fails if any upstream test itself
changed behaviour — both are read before a bump PR merges, not after an app looks wrong.

## Core codegen (#21)

React Native's TurboModule and Fabric component artifacts are generated, not shipped. Upstream produces them from
Gradle on Android and from CocoaPods on Apple, and neither exists here, so `scripts/codegen-core.ts` drives the
same `@react-native/codegen` entry points the upstream executor calls and writes the result into
`packages/core/generated`, which is checked in.

```bash
pnpm codegen   # node scripts/codegen-core.ts
```

It reads every platform-agnostic spec file under `third_party/react-native/packages/react-native/src` — the
`jsSrcsDir` React Native's own `codegenConfig` declares for its single `FBReactNativeSpec` library, and the same
directory `ReactAndroid/build.gradle.kts` passes as `jsRootDir`. That directory is vendored for this reason;
`scripts/vendor.lock.json` lists it as a sparse path. A file qualifies when its name matches `Native*` or
`*NativeComponent` with exactly one extension, it is not `NativeUIManager.js`, no directory on its path starts
with `__`, and its contents declare `extends TurboModule` or `export default codegenNativeComponent<`. That is
`combine-utils.js`' `filterJSFile` plus `combine-js-to-schema.js`' content test, restated here so the file list
can be sorted before it is merged.

It writes:

```text
packages/core/generated/
├── codegen.lock.json
├── FBReactNativeSpec/
│   └── FBReactNativeSpecJSI.h
└── react/renderer/components/FBReactNativeSpec/
    ├── ComponentDescriptors.h, ComponentDescriptors.cpp
    ├── EventEmitters.h, EventEmitters.cpp
    ├── Props.h, Props.cpp
    ├── ShadowNodes.h, ShadowNodes.cpp
    └── States.h, States.cpp
```

The two subtrees are the two include spellings the C++ sources use. `ReactCxxPlatform`'s `DeviceInfoModule.h` and
nine siblings include `<FBReactNativeSpec/FBReactNativeSpecJSI.h>`; the Fabric artifacts are included as
`<react/renderer/components/FBReactNativeSpec/...>`. `packages/core/generated` is the one include root that
answers both, and it is what `react_codegen_rncore` exports. That target keeps the `rncore` name because
`GenerateModuleJniH.js` renames `FBReactNativeSpec` to it and every ReactCommon CMakeLists in this graph links
the old name; `ReactCommon/react/renderer/components/rncore/*.h` are deprecation shims that include the
`FBReactNativeSpec` headers, so they resolve through the same directory. The five `.cpp` files are compiled; the
JSI header is header-only and its consumers are `src/TurboModuleRegistry.cpp` and, since #128,
`react/renderer/animated`, which is why `rnl_react_core` links the target rather than merely naming its include
directory, and why the target itself links `react_nativemodule_core` — see *Animated backend* below.

The generator is reached through the two entry points upstream's `scripts/codegen/generate-artifacts-executor`
reaches through `codegen-utils.js`, `FlowParser` and `RNCodegen`:

| Call | Arguments | Output |
| --- | --- | --- |
| `FlowParser#parseFile` | one spec path at a time, in sorted order | a per-file `SchemaType`, merged into one `modules` map |
| `RNCodegen.generate` | `{libraryName: 'FBReactNativeSpec', schema, outputDirectory: '<generated>/FBReactNativeSpec', packageName: 'com.facebook.fbreact.specs', assumeNonnull: false}` and `{generators: ['modulesCxx']}` | `FBReactNativeSpecJSI.h` |
| `RNCodegen.generate` | the same options with `outputDirectory: '<generated>'` and `{generators: ['componentsIOS']}` | `react/renderer/components/FBReactNativeSpec/*` |

`componentsIOS` is the generator group whose composed output folder is the `react/renderer/components/<library>`
path this build needs. It is a superset of the C++ generators by exactly one file, `RCTComponentViewHelpers.h`,
which is Objective-C++ and is deleted after generation; that deletion is the only normalisation the script
performs. `combine-js-to-schema.js` is deliberately not called: it globs with `tinyglobby`, and glob order
decides the merge order and therefore the declaration order inside every generated file. Doing the walk here,
sorting it, and merging in that order is what makes the output byte-identical from one machine to the next.
Nothing else in the generator is non-deterministic — no timestamps, no random values, no environment reads in
any template.

`codegen.lock.json` records the codegen package version and the sha256 of every input, so a generation that
predates a React Native bump is detectable without rerunning the generator. The `validate` CI job runs
`pnpm codegen` and fails on any diff under `packages/core/generated`; that is issue #85's determinism gate.

Deliberately not generated yet: third-party library specs and the autolinking that would discover them, the
`RCTThirdPartyFabricComponentsProvider` equivalent, and the JNI, Java and Objective-C++ artifact families, none
of which a Linux target can compile. Those are follow-ups to #21 and #22.

## Dimensions and TurboModules (#50)

`DeviceInfo` is the first TurboModule this platform registers, and registering it is what builds the TurboModule
path at all: before #50 nothing installed a `TurboModuleBinding`, so `nativeModuleProxy` did not exist and no
native module was reachable from JavaScript. `ReactHost` installs one from inside the same `initializeRuntime`
bindings installer that installs the console binding, over a provider that started out answering exactly one name
(`src/TurboModuleRegistry.cpp`) and is a name-to-factory map since #127. Upstream's `ReactCxxPlatform` does the
same thing through `ReactCxxTurboModuleProvider`, a chain of provider callbacks across every core module it ships;
the chain is what a platform with a dozen modules needs, and a map is what this one needs.

Two ReactCommon subdirectories join the build for this: `react/nativemodule/core`, which is where
`TurboModuleBinding` and `TurboModule::emitDeviceEvent` live, and `react/bridging`, whose `LongLivedObject.cpp`
the binding's destructor calls. `react_codegen_rncore` is linked into `rnl_react_core` for the same reason — the
generated `NativeDeviceInfoCxxSpec`, the payload structs and their `Bridging` specialisations come from
`<FBReactNativeSpec/FBReactNativeSpecJSI.h>` and `<react/coremodules/DeviceInfoModule.h>`.

`ReactInstance` defines `RN$Bridgeless` before it calls the bindings installer, so `TurboModuleBinding::install`
takes its bridgeless branch and the lookup global is `nativeModuleProxy`, not `__turboModuleProxy`. React
Native's JavaScript `TurboModuleRegistry` tries the second and falls back to the first, so a bundle that reaches
for a module by hand has to do the same.

Upstream's `ReactCxxPlatform` ships a `DeviceInfoModule`, and it is not reusable here: its `getConstants` returns
a hardcoded 1280x720 with a `TODO` to wire it to a real app size. `LinuxDeviceInfoModule` is ours, and it answers
from `DimensionsSource` (`packages/core/src/DimensionsSource.h`) instead.

### Never 0x0

`Dimensions.get('window')` on react-native-macos answers from `[NSApp keyWindow]`, which is `nil` whenever no
window has keyboard focus, and reports `{width: 0, height: 0}` for that perfectly ordinary state
(rn-macos#2296, #2129). Wayland makes the naive port worse: there is no key window, and the only authoritative
extent is the last `xdg_toplevel.configure`. So the extent lives in one place per surface and:

- it starts at the surface's requested default — 800x600 for the headless harness, the window's requested size
  for `rnl_window`, both applied before the bundle is loaded, so a `Dimensions.get` at module scope is already
  right;
- `configure` ignores any non-positive width, height or scale instead of storing it, and the last good value
  survives;
- there is therefore no ordering, no focus state and no compositor behaviour that can produce a zero.

`DimensionsTest.cpp` asserts each of those, including the explicit "no configure sequence yields a zero" case,
and `DimensionsSource.cpp` is in the 100% line-and-branch scope of `scripts/cpp-coverage.ts`.

### screen equals window

This is the `needs:decision` half of #50, and the decision is that `screen` reports the same metrics as `window`.
A Wayland client cannot ask for a screen size. The only route to one is `wl_output`'s mode, which requires
tracking `wl_surface.enter` to know which output the surface is even on, and which is still wrong the moment a
window straddles two outputs or the compositor scales the output. `WaylandWindow` binds no `wl_output` today.
Reporting the one extent that is true, twice, is better than reporting a plausible number that is not; #50 allows
the equality explicitly. If `wl_output` is bound later, `screen` is the field that changes and `window` is not.

### Scale

`scale` is always 1. Neither `wp_fractional_scale_v1` nor `wl_surface.preferred_buffer_scale` is bound, so this
client is told nothing about output scaling and 1 is the only honest answer; `fontScale` is 1 for the same
reason, as nothing reads a desktop text-scaling setting yet. `DimensionsSource::configure` takes the scale as a
parameter and stores whatever it is given, so binding the fractional-scale protocol is a call-site change rather
than a redesign. Both are the output-scale follow-up #50 names as a dependency.

### One change per frame, at most

`useWindowDimensions` re-rendering per event is its own macOS bug (rn-macos#2083), and an interactive Wayland
resize is a stream of configures. `WindowSession::resize` only records the extent; `WindowSession::deliverInput`,
which the run loop calls exactly once per frame, calls `takeChangeIfAny` and emits at most one
`didUpdateDimensions` for everything that accumulated. A configure that repeats the current metrics is not a
change at all, so a compositor that re-sends the same size emits nothing.

The event itself is `TurboModule::emitDeviceEvent`, upstream's own path: it posts through the module's
`CallInvoker` — a `RuntimeSchedulerCallInvoker` over the instance's `RuntimeScheduler` — and calls
`global.__rctDeviceEventEmitter.emit('didUpdateDimensions', payload)` on the JavaScript thread. That global is
what `RCTDeviceEventEmitter` installs in a real app, and what `Dimensions` listens on. The payload is the
generated `DimensionsPayload`: `window` and `screen`, each `{width, height, scale, fontScale}`.
`windowPhysicalPixels` and `screenPhysicalPixels` stay absent — they are the Android half, and the generated
bridging omits an empty `std::optional` rather than sending a null.

### Proving it

`packages/core/test-bundles/dimensions.js` reads the module the way `TurboModuleRegistry` does and answers the
emitter the way `RCTDeviceEventEmitter` does, because a bare bundle has neither:

```bash
hello_react --fabric packages/core/test-bundles/dimensions.js
hello_react --resize packages/core/test-bundles/dimensions.js 640 480
```

`--resize` runs the bundle at the headless surface size and then applies the pair a window applies on a
configure — `FabricHost::setSurfaceSize`, then the same extent into `DimensionsSource`, then one publish — so the
change event is reachable without a compositor. CI greps both the boot constants and the single change event.

What this does not cover: an end-to-end resize under the headless compositor. The window rig drives `weston
--backend=headless` at a fixed size and has no way to ask it to resize a client, so the compositor half of the
resize path is still proven by reasoning about `WaylandWindow::onToplevelConfigure` rather than by a running
compositor.

## Animated backend (#127, #128)

React Native 0.87 ships the whole `Animated` native driver in portable C++ —
`ReactCommon/react/renderer/animated/` is `NativeAnimatedNodesManager`, sixteen node types, four drivers, the
event driver and `AnimatedModule`, a `NativeAnimatedModuleCxxSpec` TurboModule — so a platform integrates it
rather than writing one. Background and citations are in `docs/research/animation-on-linux.md`, section 1.

### What is built

`react/renderer/animated` joins `RNL_REACT_COMMON_SUBDIRS` and `react_renderer_animated` joins
`RNL_REACT_COMMON_TARGETS`. Its own `CMakeLists.txt` names `react_codegen_rncore` and seven ReactCommon targets,
all of which were already in the closure of `react_renderer_animationbackend`. One thing was missing:
`AnimatedModule.h` includes `<ReactCommon/TurboModuleWithJSIBindings.h>` and the generated
`FBReactNativeSpecJSI.h` includes `<ReactCommon/TurboModule.h>`, and both of those directories belong to
`react_nativemodule_core`, which nothing in the animated link list names. Upstream's generated codegen target
carries that usage requirement; ours did not, so `react_nativemodule_core` was added to
`target_link_libraries(react_codegen_rncore ...)` — the one place every consumer of the spec header already links.

### The flags

`ReactNativeFeatureFlagsDefaults` answers false to `cxxNativeAnimatedEnabled`, `useSharedAnimatedBackend` and
`optimizedAnimatedPropUpdates`. `ReactNativeFeatureFlagsOverridesLinux`
(`packages/core/src/ReactNativeFeatureFlagsOverridesLinux.h`) subclasses `ReactNativeFeatureFlagsOverridesOSSStable`
and turns the first two on; `ReactHost` installs it where it used to install OSS-stable. Upstream's
`ReactNativeFeatureFlagsOverridesOSSCanary` is the class that enables `cxxNativeAnimatedEnabled`, and it is
`@generated` and carries four unrelated experiments, which is why this subclasses stable rather than adopting
canary. `optimizedAnimatedPropUpdates` stays off: its documentation describes Android JNI batching and an iOS
`cloneProps` path, neither of which is ours.

**These three flags are ours to re-verify on every React Native bump.** `packages/core/tests/FeatureFlagsTest.cpp`
reads all three back through `ReactNativeFeatureFlags` after installing the provider, plus the two OSS-stable
overrides it inherits, so a changed upstream default is a test failure rather than a silent behaviour change. It
uses `dangerouslyReset` around `override` because the accessor is process-global and throws once any flag has been
read.

`useSharedAnimatedBackend` reaches further than Animated. `Scheduler`'s constructor reads it and, when it is set,
builds an `AnimationBackend` and calls `setAnimationBackend` on `SchedulerToolbox::animationChoreographer` with no
null check — so turning the flag on makes a choreographer mandatory for every host that builds a `Scheduler`.
`FabricHost` supplies `LinuxAnimationChoreographer`; see *Animation choreographer*.

### What is registered

`TurboModuleRegistry` (`packages/core/src/TurboModuleRegistry.{h,cpp}`) grew from the one-name provider of #50
into a name-to-factory map behind the same single `TurboModuleBinding`. `DeviceInfo` is still constructed eagerly,
because the frame thread needs a handle to it; `AnimatedModule` is built per lookup as
`std::make_shared<AnimatedModule>(jsInvoker, animatedNodesManagerProvider)`, which is exactly what
`ReactCxxPlatform`'s `ReactCxxTurboModuleProvider` does. `ReactHost` owns the one
`NativeAnimatedNodesManagerProvider`, for the same reason upstream's host does: it is what makes the module and
the scheduler share a `NativeAnimatedNodesManager`.

Destruction order is load-bearing and is now explicit in `~ReactHost`: quit the JavaScript thread, release the
registry, release the manager provider, then destroy the instance. A `TurboModule` owns a `jsi::WeakObject` — the
cached JavaScript representation `TurboModuleBinding::getModule` attaches to it — and a JSI pointer that outlives
its runtime aborts a debug Hermes with *"This PointerValue was left dangling after the Runtime was destroyed"*.
`DeviceInfo` is the module that made this reachable: it is held eagerly by the registry, so before this ordering
the last reference to it was released after `reactInstance_.reset()` had already torn the runtime down, and only
on a bundle that actually looked the module up — `--resize dimensions.js` aborted at exit in the Debug CI job
while the Release presets did not. Members alone do not give this order, because the instance is reset explicitly
in the destructor body and members are destroyed only after it returns. Nothing in `TurboModuleRegistry` or
`LinuxDeviceInfoModule` caches a `jsi::Value` of its own; the representation inside the base class is the whole
of the JSI state a module carries.

Building the module per lookup is also what defers `NativeAnimatedNodesManagerProvider::getOrCreate` until
JavaScript first reaches for the module. That call resolves the `UIManager` out of the runtime through
`UIManagerBinding::getBinding`, so it only works under a Fabric host, and it picks its architecture there and then:
with an `AnimationBackend` attached to the `UIManager` it takes the shared-backend path, and without one it takes
the legacy path with `MergedValueDispatcher` and `AnimatedMountingOverrideDelegate`. The choreographer therefore
has to be in place before the first module lookup, not merely before the first animation.

### Proving it

`packages/core/test-bundles/animated.js` reads the module the way React Native's `TurboModuleRegistry` does and
queues a whole batch through it:

```bash
hello_react --fabric packages/core/test-bundles/animated.js
```

It prints `animated: module present` for a non-null `NativeAnimatedModule` and `animated: batch ok` after
`startOperationBatch`, `createAnimatedNode` twice, `connectAnimatedNodes`, `setAnimatedNodeValue`,
`startAnimatingNode` and `finishOperationBatch`, which proves registration, the JSI bindings install, and the full
argument path through the generated spec. `animated: value 0.75` is the third line, and it is the one that needs a
frame; see *Animation choreographer*. CI greps all three.

## Animation choreographer (#129)

`AnimationChoreographer` is the one part upstream's `__docs__/AnimationBackend.md` says a new platform has to
implement: "On iOS, it has a corresponding `RCTDisplayLink`, and on Android, it uses the `Choreographer`." The
interface is three calls — `resume`, `pause` and a defaulted `now` — plus a non-virtual `onAnimationFrame` that
forwards to the weakly-held `UIManagerAnimationBackend`. `LinuxAnimationChoreographer`
(`packages/core/src/LinuxAnimationChoreographer.h`) is that seam and nothing else: an atomic flag and one
forwarding call, because this platform already owns a frame source and does not need a second one.

`resume` and `pause` are `AnimationBackend::start` and `::stop`, which are called when the first render callback is
registered and when the last one goes away. They can arrive from either side — the JavaScript thread when
`AnimatedModule::finishOperationBatch` schedules a batch, the frame thread when a frame drains the last active
animation and `NativeAnimatedNodesManager::stopRenderCallbackIfNeeded` runs inside `pullAnimationMutations` — so
the flag is `std::atomic<bool>` and nothing else in the class is shared state.

The timestamp is `AnimationTimestamp`, which is `std::chrono::duration<double, std::milli>`, and the value handed
over is `steady_clock` time since epoch — the same basis as `AnimationChoreographer::now`'s default, which is why
that virtual is not overridden. It is the *tick's* timestamp, not a fresh clock read: `FrameAnimationDriver` and
the spring and decay drivers all integrate against the delta between the timestamps they are given, so a driver
stepped with a clock read taken after the frame's other work would drift away from the scroll physics and the caret
blink, which are already paced by the tick.

### The tick point, in both loops

One `tick` per drawn frame, after the frame's input has been released onto the JavaScript thread and before the
scene is taken:

- `WindowMain`'s run loop: `session->deliverInput(frameEvents)`, then `recordFrameTick`, then — only when
  `Tick::shouldDraw` — `session->tickAnimations(frameTime)` and `session->takeFrame()`. `frameTime` is read once
  and used for both the frame clock and the choreographer, so the two never disagree about when the frame was.
- `BundleRunner`'s headless loop: `deliverInputFrame` calls `dispatchInput`, `induceEventBeat`, `tickAnimations`,
  then `runUntilQuiescent` — so whatever an animation frame posts back to JavaScript is delivered by the drain that
  immediately follows it. `runFabricBundle` runs three of those frames rather than the one it used to: a headless
  run has no compositor to supply frames, and one is not enough to both step a driver and answer the read-back the
  driver's end callback asks for.

Ordering matters in one direction only: a layout-affecting animated mutation takes the Fabric-commit path, and that
commit carries `mountSynchronously = true`, so it relayouts *and* mounts on the calling thread. Ticking before
`takeFrame` is what puts it in the snapshot the same frame paints instead of the next one.

### The pending-work coupling

`isActive()` is the fourth signal in `hasPendingWork` (see *Frame clock*), reached through
`FabricHost::hasPendingWork`. Without it a running animation stops dead the moment the compositor withholds
`wl_surface.frame` — an occluded window, an inactive workspace — which is the failure #59 exists to prevent,
restated for animation. With it, and only with it, the fallback timeout keeps drawing; an idle window with no
animation still performs no GPU work, because the flag is false exactly when the backend has no callback
registered.

The loop terminates itself, which is the part worth stating: nothing calls `pause` from the outside. The frame that
finds no active animation left is the frame that calls `stopRenderCallbackIfNeeded`, which is
`AnimationBackend::stop`, which is `pause` — so an animation that ends costs exactly one more tick, and the flag it
was holding open is what delivered that tick. A choreographer that stopped being ticked once its animations ended
would never be told to pause and would keep the fallback awake forever.

### What the tick is for

A ticked choreographer with nowhere to put a non-layout frame is half a system. `AnimationBackend` splits every
frame's mutations by `hasLayoutUpdates`: with layout it commits, without it calls
`UIManager::synchronouslyUpdateViewOnUIThread`. That second half is *Sync props fast path*, below, and the two are
one mechanism read from opposite ends — this section says when a frame happens, that one says what a frame that
changes no layout does.

## Sync props fast path (#130)

`LinuxMountingManager::synchronouslyUpdateViewOnUIThread` is the platform side of `AnimationBackend`'s non-layout
half. It used to be `IMountingManager`'s defaulted no-op, which meant every animated frame that changed no layout
was silently dropped. The path in is upstream's: `AnimationBackend::synchronouslyUpdateProps` →
`UIManager::synchronouslyUpdateViewOnUIThread` → `SchedulerDelegateImpl` →
`IMountingManager::synchronouslyUpdateViewOnUIThread`. At 120 Hz the alternative is 120 Fabric commits per second
with a Yoga relayout in each one, so this is not an optimisation layered on a working path; it is the difference
between an animation that costs a matrix multiply and one that costs a relayout.

### Which props, and what they are worth

`animationbackend::packAnimatedProps` repacks `AnimatedProps` into a `folly::dynamic` object, and
`ReactCommon/react/renderer/animated/internal/NativeAnimatedAllowlist.h` is the upstream oracle for which keys can
appear in it, and `kAnimatableProps` — see *Native-driver allowlist* — is the one place the three below are
written down. Three are applied:

- `opacity`, a double, clamped to `[0, 1]` exactly as `readPaintProps` clamps `ViewProps::opacity`. The snapshot
  folds it into every colour the subtree paints, so an opacity ramp is arithmetic on the alpha channel and nothing
  else.
- `backgroundColor`, a packed int32 ARGB — the same representation `HostPlatformColor` uses on this platform, so
  it is a `SharedColor` cast rather than a conversion. A fully transparent colour is stored as no colour at all,
  which is what `meaningfulColor` decides for the commit path too.
- `transform`, the raw JS operation array. It is read back through `fromRawValue` — the same overload `ViewProps`
  parsing uses, so `{"translateX": 20}`, `{"rotateZ": <radians>}` and `{"translateX": "50%"}` mean here exactly
  what they mean in a commit — and then resolved by the static `BaseViewProps::resolveTransform(frameSize,
  transform, transformOrigin)`, which is the same cascade `readPaintProps` reaches through the member overload.
  The frame is load-bearing: a percentage translation is only meaningful against the node's own size. So is the
  origin, which is why `SceneNode` now retains `transformOrigin` unresolved next to the resolved matrix — a
  `transform` arrives without one, and resolving it about the centre when the node mounted with a corner origin
  would make the fast path paint a different picture from the commit path.

Everything else the allowlist permits — border and shadow colours, border radii, `elevation`, `zIndex`,
`shadowOpacity`, `shadowRadius`, the legacy `scaleX`/`translateY` style operations — is **rejected, counted and
named**, not applied. Border colours are the nearest follow-up: they live inside `BorderMetrics`, which
`resolveBorderMetrics` cascades out of nine props, so a single animated edge colour is a re-cascade rather than a
field write and belongs with whoever needs it first.

### The diagnostic policy

The same policy as the missing-tag one, for the same reason: silence is the bug.

- An unknown tag goes through `verifyTagIsKnown`, which now returns whether the tag was known. The miss is counted
  in `MountDiagnostics::unknownTagOperations` under the operation name `SyncUpdate`, logged once with the tag and
  the last transaction number, and then the update is **dropped** — nothing is written, no damage is recorded, and
  `hasPendingDamage_` stays as it was, because damage a frame cannot satisfy is worse than a missed frame.
- A prop outside the applied three, and an allowlisted prop carrying a non-finite value, both increment
  `MountDiagnostics::rejectedAnimatedProps`; the first one is kept in `firstRejectedAnimatedProp` and logged once,
  with the message `rejectedAnimatedPropMessage` composes. The rest of the payload still applies, exactly as a bad
  mutation never stops the rest of its transaction. See *Native-driver allowlist* for the table both the applying
  and the naming read from, and for the exact text.
- Nothing on this path throws. A payload that is not an object is a no-op, and no prop value is trusted far enough
  to matter beyond the type `packAnimatedProps` gives it.

### No commit, and the mutex that makes that safe

`RetainedScene::applyAnimatedProps` is one entry point rather than three setters because the damage has to bracket
the whole payload: `damageSubtree(tag)` before the writes and again after, which is what `updateNode` does and what
makes a node that moved repaint where it was as well as where it is. The manager then sets `hasPendingDamage_`, so
a window pacing off a withheld `wl_surface.frame` still knows there is something to draw (see *Frame clock*).

No `ShadowTree` commit happens on this path and none is wanted: the shadow tree is not the source of truth for an
animated value between commits — `AnimationBackendCommitHook` re-applies animated props on top of whatever React
commits next, which is what keeps a rerender mid-animation from painting a stale frame. The write is to the
retained scene only.

The call arrives on the frame thread, inside the animation tick that runs just before `takeFrame`, while the
JavaScript thread may be committing. `sceneMutex_` is therefore the only thing keeping the scene consistent — the
same statement this file already makes about `resize` — and it is the same mutex `takeFrame` takes, so a
synchronous update is never half-visible to the frame that follows it.

### The proof

`packages/core/tests/AnimatedPropsTest.cpp`, inside the 100% line and branch gate that covers
`LinuxMountingManager.cpp` and `RetainedScene.cpp`:

- an `opacity` update folds into the painted alpha, damages exactly the node's bounds, and is carried by the next
  `takeFrame`, which clears the pending flag;
- a packed int32 `backgroundColor` becomes the painted colour, and a fully transparent one leaves the node
  painting nothing;
- a `transform` array — translate then rotate, and a percentage translation that needs the frame — produces the
  **same matrix** as mounting the same `Transform` on `ViewProps` through a mounting transaction, and resolves
  about the `transformOrigin` the node mounted with. The payload is built with upstream's own
  `updateTransformProps`, which is the serializer `packAnimatedProps` uses, so the test parses what the driver
  would have sent;
- an unknown tag is a `SyncUpdate` diagnostic with no damage and no pending flag;
- a non-allowlisted key is counted with its name while the rest of the payload still applies, and a second
  rejection is counted without displacing the first.

### Follow-ups

- **Golden-image.** An opacity ramp and a translate ramp at three fixed progress values, driven by the injected
  clock, are still owed; the unit equality above says the fast path computes what the commit path computes, not
  that Skia paints it identically. Both remain #130 acceptance criteria that this change does not close.
- **E2E and perf.** The 120-frame transform animation that performs **no Fabric commit while it runs** and exactly
  one when it settles — see *The one commit an animation does make* — asserted from the event trace, and its p95
  frame time under #20's harness, are also still owed: a unit test can count the commits a manager asked for, but
  only the harness can observe what the surface actually did with them.
- **TSan.** Synchronous updates on the frame thread concurrent with commits on the JavaScript thread need the
  sanitizer job, like `ShadowTreeCommitTest`'s cross-thread case.
- **Border colours.** Rejected and counted today; see above for why they are a re-cascade rather than a write.

## Event-driven Animated (#131)

`Animated.event([{nativeEvent: {contentOffset: {y}}}], {useNativeDriver: true})` is the other half of the native
driver, and it is a different code path from a time-driven animation: an `EventAnimationDriver` per
(view tag, event name), fed by an `EventEmitterListener`, updating a value node from a payload path. React Native
0.86 fixed a one-frame latency in it — *"processing the animation graph synchronously on every scroll event,
matching the Java implementation"* — and this section is where our frame-batched event delivery and that
synchronous requirement are reconciled.

### The listener came for free

Nothing in `FabricHost` registers an `EventEmitterListener`, and nothing needs to.
`NativeAnimatedNodesManagerProvider::getOrCreate` calls `uiManager->addEventListener(...)` **after** its
if/else, so both the legacy path and the shared-backend path this platform takes get it, and the listener it
registers forwards into the `EventEmitterListenerContainer` the manager's own listener was added to two lines
earlier. From there the chain is entirely upstream's: `UIManager::addEventListener` →
`Scheduler::uiManagerShouldAddEventListener` → `EventDispatcher::addListener`, and both
`EventDispatcher::dispatchEvent` and `dispatchUniqueEvent` consult the listeners **before** they enqueue.

Two consequences are ours, and they are what makes this work rather than compile:

- **`ScrollViewEventEmitter::onScroll` is `dispatchUniqueEvent`**, so the driver sees every `onScroll` the
  controller emits, on the thread that emitted it — the frame thread — synchronously inside `advanceScroll`,
  before the event is queued and long before the JavaScript thread sees it. Coalescing is downstream of the
  listener: a beat that collapses two frames' `onScroll` into one for JavaScript still gave the driver both.
- **The registration is deferred to the first module lookup.** `getOrCreate` runs from
  `AnimatedModule::installJSIBindingsWithRuntime`, which `TurboModuleRegistry` reaches when JavaScript first asks
  for `NativeAnimatedModule` (see *Animated backend*). A bundle that never touches Animated registers no listener
  and pays nothing.

`NativeAnimatedNodesManager::handleAnimatedEvent` then rejects any event that does not arrive on the thread an
animation frame has run on — `isOnRenderThread_` is a `thread_local` set inside `pullAnimationMutations`. The
frame thread claims it on the first frame that ticks the choreographer after the bundle's operation batch, which
in a window is the first drawn frame and in a headless run is one deliberate warm-up frame; see *The proof*.

### The order inside a frame

`FabricHost::advanceScroll` is the one call site, so the window loop and the headless runner cannot disagree
about it. Reading `WindowSession::deliverInput` and `WindowMain`'s loop top to bottom:

```text
publishPendingDimensions
advanceCaretBlink
dispatchInput                    scroll routed to a ScrollView, pointer to InputDispatcher
advanceScroll                    ── one frame of ScrollPhysics per axis, per ScrollView
  ├─ ConcreteState::updateState      contentOffset queued for the JavaScript thread
  ├─ ScrollViewEventEmitter          onScroll → EventDispatcher listeners, on this thread:
  │    └─ EventAnimationDriver           the value node takes contentOffset.y
  └─ UIManagerAnimationBackend::trigger  the graph is walked and the props applied, here, now
induceEventBeat                  everything the frame queued is released onto the JavaScript thread
recordFrameTick
tickAnimations                   the choreographer's frame, for time-driven animations
takeFrame                        the scene the compositor is shown
```

**The synchronous animation push sits after `advanceScroll`'s state write-back and its `onScroll`, and before
`induceEventBeat`.** That is the ordering statement #131 asks for. It is inside `advanceScroll` rather than beside
it, and it is skipped on a frame that dispatched no `onScroll`, which `ScrollController::hasDispatchedScrollEvent`
answers without a `UIManager` lookup.

`trigger()` is upstream's own entry point for exactly this — `AnimationBackend::trigger` is `onAnimationFrame` with
a fresh `steady_clock` read, and its documentation calls it the path "for updates that need to be applied
synchronously (e.g. gesture events)". The mutations it produces have no layout in them for a `translateY`, so they
take the *Sync props fast path* to `RetainedScene::applyAnimatedProps` on this same thread, under the same
`sceneMutex_` `takeFrame` takes. No Fabric commit happens, and the value is in the scene the frame paints.

Upstream's `handleAnimatedEvent` already calls `pushAnimationMutations` itself, inside the listener, which is the
0.86 fix. The platform's `trigger()` is therefore not the only mechanism — it is the one this platform can state a
frame order about. It costs one graph walk per scrolling frame and re-applies values that are already current;
what it buys is that the same-frame guarantee holds for the whole frame's worth of scroll events rather than for
whichever of them got past a `thread_local` check inside a vendored file.

One asymmetry is worth naming: **the animated value is never behind the scroll offset, and can be ahead of it.**
The offset reaches the scene through `updateState` → beat → commit → mount on the JavaScript thread, while the
animated prop reaches it on the frame thread before the beat is even induced. A busy JavaScript thread therefore
delays the content and not the header. That is the safe direction — a parallax header that lags is the artefact
#131 exists to prevent — but it is a property of *ScrollView*'s state write-back, not of this section.

### The proof

```bash
hello_react --animated-scroll packages/core/test-bundles/animated-scroll.js 160 100 3
```

`packages/core/test-bundles/animated-scroll.js` is a 200x150 `<ScrollView>` over 470 points of content, plus a
100x100 view **outside** it whose `translateY` is driven by the ScrollView's `contentOffset.y`. There is no React
and no Animated JavaScript in a bare bundle, so it builds the graph `Animated.event` would have built directly
through `nativeModuleProxy.NativeAnimatedModule`: a value node, a `transform` node reading it, a `style` node, a
`props` node, `connectAnimatedNodeToView`, and
`addAnimatedEventToView(scrollViewTag, 'topScroll', {nativeEventPath: ['contentOffset', 'y'], animatedValueTag})`.
`EventEmitter::normalizeEventType` is what makes `topScroll` here and `scroll` on the emitter the same key.

`connectAnimatedNodeToShadowNodeFamily` is the call the fixture would silently die without: in the shared-backend
path `NativeAnimatedNodesManager::insertMutations` **drops** any update whose props node has no
`ShadowNodeFamily` behind it, and `connectAnimatedNodeToView` carries only the tag. The bundle throws if the
module does not expose it rather than printing a trace that proves nothing.

`--animated-scroll` (`BundleRunner::runAnimatedScroll`) drives the glide one frame at a time in the window loop's
order, inducing the beat **per frame** — the opposite of `--scroll-to`, which induces once at the end because it
only cares where the content stopped. It spends one warm-up frame before the wheel, because the bundle's operation
batch is drained by an animation frame and because that frame is what claims the frame thread as the manager's
render thread.

Each frame prints one line, from JavaScript:

```text
animated-scroll: offset 3.87 value 3.87
...
animated-scroll: offset 120.00 value 120.00
```

The offset is the frame's `topScroll` payload and the value is the animated node's, delivered through
`onAnimatedValueUpdate` on `__rctDeviceEventEmitter`. The two arrive on the JavaScript thread in that order — the
value's `invokeAsync` is scheduled from inside the event's own dispatch, the beat is induced after
`advanceScroll` returns, and both are `ImmediatePriority` work on the `RuntimeScheduler` — which is what lets one
line carry both numbers. A value applied a frame late prints the previous frame's number on the right of every
line, so the CI step greps a backreference (`offset ([0-9]+\.[0-9]{2}) value \1`), asserts that **no** trace line
fails it, and asserts the settled `offset 120.00 value 120.00` that three 40-point notches have to end on.

### Tests

The event driver's own unit coverage is upstream's and already runs here:
`ReactCommon/react/renderer/animated/tests/EventAnimationDriverTests.cpp` is compiled into `rnl_core_tests` by
#132, and it is the test that asserts a value node takes `contentOffset.y` from a synthesised `ScrollEvent`
through `getEventEmitterListener()`, with a second driver on `zoomScale` proving the path resolution. We do not
write the animated node graph, so we do not write its tests either — see *Upstream animated tests*.

No new pure logic landed in the coverage gate. The only platform state this added is
`ScrollController::hasDispatchedScrollEvent`, and `ScrollController.cpp` is outside `scopedSourcePaths` for the
reason *ScrollView* gives: what is in it is a `UIManager` hit test, an ancestor walk and upstream calls that need
a committed shadow tree. The trace above is its test, at the same level `--scroll-to` is.

### Deferrals, with owners

- **Detach.** `removeAnimatedEventFromView` is never called by this fixture, and nothing on this platform
  unregisters a driver when the view it is attached to unmounts — upstream drops the drivers with the node graph
  when JavaScript says so, which is the only path React Native ever uses. #131's *"detaching mid-scroll stops
  delivery without touching a removed node"* is asserted by nothing here; it needs a fixture that scrolls,
  detaches and scrolls again, and it belongs with whoever writes the first JavaScript-side `Animated` surface.
- **Golden.** A picture of the header and the content at three scroll positions is still owed, for the same
  reason *Sync props fast path* owes one: the trace proves the numbers agree, not that Skia paints them.
- **E2E under a compositor.** `rnl_inject` grew a `wheel up|down <notches>` command with #244, so a scroll step is
  writable now; what is still owed here is the animated-header trace itself, which needs a fixture that scrolls
  and reads the driven prop back, not a new injector capability.

## Hit-testing under animation (#97, #121)

After #130 a native-driven `opacity`, `backgroundColor` or `transform` animation writes the retained `SceneNode`
every frame and performs **no Fabric commit while it runs** — the one it does perform is at the end, and *The one
commit an animation does make* below is what that is for. That is the right thing for the picture and it splits
geometry in two: the painter reads the animated scene, and everything that reads the committed shadow tree — hit
testing,
`measure`, `getRelativeLayoutMetrics` — reads the value the node last mounted with. Upstream shipped exactly this
split and [core#51621](https://github.com/facebook/react-native/issues/51621) has been open ever since: `onPress`
does not fire while a view animates on the new architecture, because
[PR 43374](https://github.com/facebook/react-native/pull/43374) syncs the animated value back into the shadow tree
only when the animation **ends**. Reproducing it here by design was the default, and #97 exists to not take it.

### The decision

**Hit testing reads the retained scene.** `RetainedScene::findNodeAtPoint(rootTag, surfacePoint)` returns a
`SceneHit` — the deepest painted node under the point and the absolute origin it was painted at — and
`InputDispatcher::resolveTarget` asks it first. `UIManager::findNodeAtPoint` is kept only for a painted tag that
the committed shadow tree does not contain.

The alternative was upstream's: keep the shadow-tree hit test and fold the animated value into a mounting override
so the tree stays consistent. It was rejected on cost. A mounting override is a commit per animated frame, with a
Yoga relayout inside it — the exact cost *Sync props fast path* exists to avoid — and it makes correctness of the
click depend on a commit landing before the next press, which is a race rather than a guarantee. The retained
scene, by contrast, already holds everything a hit test needs and already holds it in the composed form the
painter uses: absolute frames, the composed 2D affine matrix, and the inherited `overflow: hidden` clip stack.
Reading it costs one tree walk and no commit, and it makes the answer true on **every** frame rather than after
the last one.

The objection this replaces — recorded in the old *Input* text — was that "a second hit-test implementation would
be a second chance to disagree with React about what was clicked". That is answered by construction rather than by
avoidance: `findNodeAtPoint` calls the same `visitNode` that `appendPrimitives` calls, so the frame, the matrix
and the clips a press is decided against are the same objects the next snapshot paints. There is one composition,
not two. What the scene does not have — event emitters — is not duplicated either: the scene answers with a Fabric
tag and the shadow tree is searched for that tag, so the node React is told about is still a committed shadow node.

### The algorithm

`hitTestNode` is a pre-order walk carrying a `ScenePaintState`, the same struct the snapshot walk carries:

1. Visit the node with `visitNode`, producing the primitive it would paint and the state its children inherit.
2. If the node's `pointerEvents` allows children to be targets (`auto` or `box-none`), recurse into `childTags`
   **backwards**. Child order is mount order is paint order, so the last sibling painted is the one on top, and
   the first hit found front-to-back wins.
3. Otherwise, or if no child was hit, the node itself is the answer when its `pointerEvents` allows it to be a
   target (`auto` or `box-only`) and the point lands on it.
4. "Lands on it" is the inverse of the composed matrix. The surface point is mapped back into the untransformed
   coordinates the node's frame is written in, and the frame is tested for containment. Every inherited clip is
   tested the same way, through its own matrix, because a node paints only inside the `overflow: hidden`
   ancestors that cut it. A matrix whose determinant is under `1e-6` — `scale: 0` — maps the node onto a line and
   is never hit, which is also what it paints.

The inverse rather than upstream's forward-mapped bounding box is what makes a rotated node hit where it is drawn
rather than inside the axis-aligned box around it, and it is why the vertical- and horizontal-inversion special
cases `LayoutableShadowNode::findNodeAtPoint` carries have no equivalent here: an inverted matrix inverts.

Four rules are stated rather than inherited:

- **`pointerEvents` is scene state.** `SceneNode` retains it — the only prop it keeps that paints nothing —
  because the painted-geometry answer still has to obey the same rule
  `ConcreteViewShadowNode::canBeTouchTarget` and `canChildrenBeTouchTarget` apply to the shadow tree.
- **A node animated to `opacity: 0` is still hittable**, exactly as a statically transparent one is. The scene
  drops invisible nodes from the *snapshot*, never from the tree, so the hit walk still sees them. Upstream chose
  the same, and [core#50465](https://github.com/facebook/react-native/issues/50465) is iOS choosing the opposite
  by accident.
- **A node that paints nothing is still a target.** An empty `<View>` contributes no primitive and is still
  pressed, which is what a `<Pressable>` with no background needs.
- **Corner radii are consulted**, which is issue #99 and a departure from upstream. A press outside a rounded
  card's corner arc misses the card, and a press outside a rounded `overflow: hidden` ancestor's arc misses
  everything inside it. `coversPrimitive` asks `roundedBoxContainsPoint` of the `roundedBorderBox` of the
  primitive and of each clip — the same three functions the painter fills, rings and clips with, so the corner a
  press misses is a corner no pixel was painted in. Upstream's `LayoutableShadowNode::findNodeAtPoint` presses
  the bounding rectangle instead, which is why a `<Pressable>` ripple can paint outside its own rounded border
  there ([core#34553](https://github.com/facebook/react-native/issues/34553)). See *One rounded box* under *View
  props fidelity*.

There is no re-sync step at animation end, because nothing was ever wrong: the scene held the current value on
every frame, so the frame after the last one changes no answer.

### What stays on the UIManager path

- **A painted tag with no shadow node.** `resolveTarget` falls back to `UIManager::findNodeAtPoint` when the tree
  does not contain the tag the scene named — a node from another surface, or a revision this thread has not seen.
- **Scroll routing.** `ScrollController` still hit-tests through `UIManager::findNodeAtPoint`, because a wheel
  moves the deepest `<ScrollView>` under the pointer and a ScrollView's own frame is not what an animation moves.
  Moving it is a follow-up, not a correctness bug today.
- **Focus traversal.** The focusable set is a walk of the committed shadow tree, filtered by props, and it is not
  a geometry question at all. See *Focus and keyboard*.
- **Keyboard activation.** `emitActivation` measures the synthetic click at the focused node's layout origin
  through `getRelativeLayoutMetrics`. It is not a hit test, and the coordinates of a keyboard press are not a
  claim about where anything was painted.

### What `measure()` reports

**Layout geometry, unscaled and un-animated** — the same answer `measure`, `measureInWindow` and `measureLayout`
give when nothing is animating. This is a decision that needs no code: those APIs resolve through
`UIManager::getRelativeLayoutMetrics` on the committed shadow tree, and the fast path writes only the scene, so
the values are already the laid-out ones. It is recorded here so it is a contract rather than a side effect.

The reasons it is the right answer rather than merely the cheap one:

- It is what #115 in this tracker asserts, and what
  [core#54988](https://github.com/facebook/react-native/issues/54988) reports as a bug in the other direction:
  `measure()` returning doubled values under a scaled parent is paint geometry leaking into a layout API.
- `measure` is what layout code reads to place things. A value that moved 60 times a second would make every
  consumer of it race the animation.
- Hit testing is the one caller that must be paint-true, because it answers a question about pixels the user
  pointed at. It no longer goes through `measure`, so the two can hold different answers honestly.

The consequence to know: during a native-driven transform animation, `measure()` and the pointer's target
disagree, on purpose. A component that measures itself and then compares the result with a pointer coordinate is
asking two different questions.

### The one commit an animation does make

`measure()` is *eventually* consistent, and the thing that makes it so is upstream's end-of-animation re-sync —
the same [PR 43374](https://github.com/facebook/react-native/pull/43374) that #97 and #121 exist to survive. It is
kept, unchanged, because with hit testing off the shadow tree it is no longer load-bearing for input and is
exactly right for layout.

The mechanism, read from the frame that runs it: `NativeAnimatedNodesManager::onAnimationFrame` collects the value
nodes whose drivers reported `getIsComplete()` into `finishedAnimationValueNodes` and hands them to `updateNodes`,
whose BFS marks everything downstream of a finished value as `connectedToFinishedAnimation` and calls
`PropsAnimatedNode::update(forceFabricCommit = true)` on the props node it reaches. Every other frame calls
`update()` with that flag false. So the count is exactly **zero commits per frame while an animation is in flight,
and one when it finishes**, whatever the animation's length.

What that one commit does differs by configuration, and the window runs the second:

- **Direct-manipulation configuration** (`schedulePropsCommit`'s non-shared branch, which is what the unit test
  builds): `forceFabricCommit` puts the props into `updateViewProps_` **and** `updateViewPropsDirect_`, so
  `commitProps` calls the Fabric callback and the direct one, in that order. The comment upstream leaves on the
  second — "must call direct manipulation to set final values on components" — is the statement that the picture
  is still the fast path's even on the settling frame.
- **Shared-backend configuration** (`useSharedAnimatedBackend`, which *The flags* turns on): `forceFabricCommit`
  inserts the tag into `shouldRequestAsyncFlush_`, the mutation still travels the non-layout half, and
  `AnimationBackend::requestAsyncFlushForSurfaces` posts an **empty commit** onto the JavaScript thread so
  `AnimationBackendCommitHook` pushes the animated props into the shadow tree React sees. Same purpose, no
  props payload, one commit.

Nothing about hit testing depends on that commit landing, which is the difference from upstream: there, the
re-sync is what eventually makes a click work again, and here it is only what eventually makes `measure()` agree.
The scene held the current value on every frame either way.

### The proof

`packages/core/tests/AnimatedHitTestTest.cpp`, inside the 100% line and branch gate that covers
`RetainedScene.cpp` and `LinuxMountingManager.cpp`:

- `ATranslationAppliedWithoutACommitMovesWhereAPressLands` is the issue. A `transform` translation is applied
  through `LinuxMountingManager::synchronouslyUpdateViewOnUIThread` — no commit, no mutation — and the node is
  then found at its new painted centre and **not** at the position it mounted at.
- `ThePressedOriginIsThePaintedFrameMappedByThePaintedMatrix` is the one-computation claim: the origin the hit
  reports is recomputed from the snapshot's own matrix and frame, so changing either half alone fails it.
- `ANodeAnimatedToFullTransparencyPaintsNothingAndIsStillPressable` pins the `opacity: 0` rule against a snapshot
  that is empty.
- `AnOverflowHiddenAncestorClipsWhatCanBePressed`, the three `PointerEvents*` cases,
  `TheSiblingPaintedLastIsTheOneThePointFinds`, `ANodeScaledToNothingCannotBePressed` and the surface-root cases
  cover the rest of the walk.
- `packages/core/tests/BorderGeometryTest.cpp` is the rounded half, issue #99:
  `APressJustInsideARoundedCornerMissesTheNodeNoPixelWasPaintedFor`,
  `ARoundedOverflowHiddenAncestorClipsAPressToItsOwnCorner`, and
  `TheHitRegionIsTheSameRoundedBoxTheSnapshotIsPaintedWith`, which samples a grid over the node and asserts that
  the hit test and `roundedBoxContainsPoint` of the snapshot's own box agree at every point.
- `AnimatedFrameAgreementTest.EveryTickLeavesTheSceneAtTheValueTheDriverJustHandedOver` is #121. It builds a real
  `NativeAnimatedNodesManager` — value → transform → style → props → view, a `frames` driver, an injected
  `g_setNativeAnimatedNowTimestampFunction` clock — whose direct-manipulation callback is
  `synchronouslyUpdateViewOnUIThread`. The graph is installed through `scheduleOnUI`, which is the batch
  `AnimatedModule::finishOperationBatch` queues, so it is built on the render thread inside the first tick exactly
  as it is in the window. For each of three ticks it asserts that one payload arrived (no lag, nothing a frame
  late), that the snapshot's matrix equals the translation the driver handed over, and that the hit test finds the
  node at that painted centre with that painted origin. It also records the Fabric commits each tick caused and
  asserts the sequence is `{0, 0, 1}`: nothing commits while the animation is in flight, and the tick the frames
  driver completes on carries the one re-sync *The one commit an animation does make* describes. That tick is
  pinned by the final value being exactly `toValue` rather than by its index, and the clean `MountDiagnostics`
  afterwards is the third consumer — the painter's read, the hit test and the diagnostics all derived from one
  scene state.

The manager in that test is built with the direct-manipulation constructor rather than the shared-backend one the
window uses. The shared-backend path needs an `AnimationBackend`, a `UIManager` and a live `ShadowNodeFamily` per
animated node — `insertMutations` drops a mutation whose weak family does not lock — and none of those exist in a
Hermes-free unit binary. The payload is identical either way: `TransformAnimatedNode::collectViewUpdates` produces
the same raw operation array that `animationbackend::packAnimatedProps` serializes, which is what the fast path
parses.

### Follow-ups

- **Golden-image.** A frame captured mid-animation, so the painted position the hit test is asserted against is a
  reviewed artifact rather than a computed one. Still a #97 acceptance criterion; this change does not close it.
- **E2E under cage.** #97 asks for `--inject-pointer` at a scripted offset into a running animation, and #121 for
  the same through the compositor. The rig exists — `scripts/e2e.ts` and `packages/core/e2e/*.json`, see *E2E
  driver* — and the scenario is not written yet, because the bundle it needs does not exist. It has to combine
  `pressable.js`'s hand-built fiber-shaped instance handles with `animated.js`'s operation batch, and in the
  shared-backend configuration the window runs, a batch is not enough: `connectAnimatedNodeToView` records a tag
  and no family, and `NativeAnimatedNodesManager::insertMutations` skips any mutation whose weak
  `ShadowNodeFamily` does not lock. The bundle therefore also has to call `connectAnimatedNodeToShadowNodeFamily`
  with the object `nativeFabricUIManager.createNode` returned. Its timing is coupled to *Animation choreographer*'s
  pending-work rule: the animation only keeps ticking while `LinuxAnimationChoreographer::isActive` holds the
  fallback timeout open, so the scenario's `frames` budget and its `sleep` steps have to bracket the animation
  rather than the other way round. Owner: whoever writes the first animated fixture bundle.
- **`ScrollController`.** Its hit test is still the shadow tree's; see above.
- **TSan.** The scene hit test runs on the frame thread under `sceneMutex_` while the JavaScript thread commits.
  It joins the same sanitizer gap *Sync props fast path* already names.
- **`InputDispatcher.cpp` is outside the C++ coverage gate**, because it needs a `UIManager` and is not compiled
  into `rnl_core_tests` — `scripts/cpp-coverage.ts`'s `scopedSourcePaths` has never listed it. The scene half of
  this change is fully gated; the dispatcher half is proved only by `--inject-pointer` and by the e2e scenario
  above.

## The animation that outlives its view (#74)

`react-native-windows` [#16309](https://github.com/microsoft/react-native-windows/issues/16309) is a **permanent
UI thread hang**: an unbounded `ProcessDelayedPropsNodes` retry loop when a native-driver animation targets a view
the Fabric registry does not hold. The animated node graph and the mounting layer have independent lifetimes, so a
driver can name a tag that was never mounted, was unmounted mid-animation, or was mounted again after that — and
the thread that answers is the one that draws. ADR-0001 puts us in exactly that configuration: the shared C++
animated graph, driven from a platform-owned frame thread, where an unbounded retry has no way out but SIGKILL.

**It cannot happen here, and the reason is an absence:** there is no delayed-props queue.
`synchronouslyUpdateViewOnUIThread` asks the scene once, counts a diagnostic if the tag is unknown, and returns.
Nothing is parked, so nothing is retried.

That is only true while nobody adds a queue, so `AnimatedPropsLifetimeTest` writes the answer down:

- **An animation that outlives its view** costs one diagnostic per frame and nothing else. 120 frames after the
  unmount produce 120 counts, no damage, and an empty frame — bounded work, in the shape a report of it would
  need: the first unknown operation, tag and transaction number are kept whole.
- **An animation attached before its view mounts** applies from the mount onwards. The tag is not poisoned by
  having been unknown for 120 frames.
- **A tag mounted again after its unmount** animates as the *new* node: the second node mounts red and animates to
  half red, so nothing survived the unmount that should not have.
- **Animating while another thread mounts and unmounts the same tag** is serialized by `sceneMutex_`, which both
  sides take. The assertions only say the run finished and the scene holds either the node or nothing;
  ThreadSanitizer is what checks the claim, and the CI TSan job runs this suite.

## pointerEvents across two levels (#64)

`pointerEvents` is two predicates in `RetainedScene` — `isPointerTarget` (is this node itself pressable) and
`arePointerChildrenTargets` (does the walk descend into it) — and the four values are the four combinations.
react-native-windows spent a decade finding those combinations one report at a time
([#8496](https://github.com/microsoft/react-native-windows/issues/8496)), so
`PointerEventsComposeAcrossTwoLevelsAsTheTableSays` in `AnimatedHitTestTest.cpp` is the whole contract at nesting
depth two: the parent's value crossed with the child's, sixteen rows, probed inside the child and inside the parent
only. The row that matters most is the inherited one — a `none` parent makes an `auto` child unreachable because
the walk never descends into it — and it needs no special case, because it is what the two predicates compose to.

**The prop is re-read on every commit.** `readPaintProps` runs from `writeNode`, which `updateNode` goes through,
so a `pointerEvents` that changes from `none` to `box-none` changes the hit answer in the commit that changed it.
That is [#10493](https://github.com/microsoft/react-native-windows/issues/10493) — the prop applied once at
mount and never invalidated — and `ChangingPointerEventsInALaterCommitChangesTheAnswerInThatCommit` is the test.

The two other halves of the issue were already proved elsewhere and are pointed at rather than re-proved: a
rounded corner is transparent to the pointer because the hit region is `roundedBoxContainsPoint` of the same box
the painter fills (#99), and an ancestor's `overflow: hidden` removes the clipped area from the hit region because
`--hit-paint-golden` compares the two at every pixel (#35).

## Animation frame cost (#124)

Upstream shipped a 20× native-driver animation regression to a stable release
([core#50716](https://github.com/facebook/react-native/issues/50716)) and CI did not catch it. Allocation is the
mechanism behind that class of regression, so #124's unit half is a **zero-allocation assertion on the frame-thread
animation path**: not "is it fast today" but "does a steady state frame reach the allocator, and how often". The
number is a gate rather than a note, because a per-frame allocation that appears in a refactor is invisible in a
p95 until the machine is loaded, and by then it is in a release.

### The probe

`packages/core/tests/AllocationProbe.h` replaces the global allocation operators with counting `std::malloc` and
`std::free`. Two properties keep that safe inside a binary that also runs every other suite:

- **Counting only.** No pooling, no poisoning, no bookkeeping header on the allocation. Nothing a test can observe
  except the counters, so no other suite's behaviour changes when this header is linked in.
- **Only inside an `AllocationScope`, only on that scope's own thread.** The counters and the arming flag are
  `thread_local` and the flag is false everywhere outside a scope, so GoogleTest's own allocations — registering
  tests, formatting failures, building strings for `EXPECT_*` — are never counted. That is also why every
  assertion in the suite reads the counters inside the scope and asserts on the copies afterwards.

Global operator replacement is program-wide by nature; what is scoped is the counting, not the replacement. The
operator definitions are therefore deliberately **not** `inline`, so a second translation unit that includes the
header is a duplicate-symbol link error rather than a silent second replacement. Today that one includer is
`AllocationCostTest.cpp`, which is why the animation frame cost of #124 and the mounting cost of #106 are
measured in one file: there is exactly one place the allocator can be watched from. Over-aligned allocations are left to the library's own
`operator new(size_t, align_val_t)`, which the library pairs with its own aligned delete: the counts stay balanced
and the uncounted path is one nothing on the animation frame path takes.

The header and the test are **outside** the 100% coverage gate — `scripts/cpp-coverage.ts`'s `scopedSourcePaths`
only ever lists `packages/core/src` files. Nothing under `src` changed for this, so every gated file keeps the
coverage it already had.

### What is measured

`packages/core/tests/AllocationCostTest.cpp` drives the two calls the window makes between delivering a
frame's input and taking its scene, after two warm-up frames — the first frame through any of these paths also
pays for the runtime's, the feature flags' and the containers' one-time initialisation, and a gate that measured
that would be measuring startup.

Every case asserts twice: `EXPECT_LE` against a stated ceiling, and `EXPECT_EQ` between two consecutive steady
state frames. The equality is the assertion that actually catches a regression — a path that starts allocating per
frame stops being equal to the frame before it long before it crosses any ceiling — and the ceiling is what stops
the equality from being satisfied by a uniformly expensive frame. Each test prints the count it measured, so the
numbers are tracked rather than only gated.

| Path | Asserted | Why |
| --- | --- | --- |
| `LinuxAnimationChoreographer::tick` | **0 allocations, 0 deallocations** | An atomic load, a `weak_ptr::lock`, one virtual call. The test's backend records into two scalars rather than a vector, so the measurement is of the path and not of the recorder. |
| `synchronouslyUpdateViewOnUIThread`, `opacity` | ≤ 12, equal frame to frame | Six by construction, all of them the damage bracket — see below. The ceiling is set at twice the read count so an incidental container in the subtree walk is not a build break. |
| `synchronouslyUpdateViewOnUIThread`, `backgroundColor` | ≤ 12, equal frame to frame | Same six. `SharedColor` is an `int32_t` wrapper on this platform, so the colour itself costs nothing. |
| `synchronouslyUpdateViewOnUIThread`, `transform` | ≤ 64, equal frame to frame | The six above **plus a `folly::dynamic` parse that inherently allocates** — see *The transform ceiling*. |
| `RetainedScene::takeDamage` | **0 allocations** | `std::exchange(damage_, SceneDamage{})` is a move; the empty replacement never allocates. |
| `RetainedScene::applyAnimatedProps` damage | ≤ 12, equal frame to frame | The same six, measured at the scene rather than at the manager. |

The six are all damage, and none of them are the payload: `applyAnimatedProps` brackets its writes with
`damageSubtree` before and after, each run builds one `std::vector<const SceneNode*>` of ancestors and one
`SceneSnapshot` of the subtree's primitives, and each then appends one rectangle to `damage_`. The prop names are
short enough for the small-string buffer, the rejected-prop list stays empty, and the node's own containers are
empty, so nothing else on the path is a container at all.

Two of the six are the damage list itself, and that is the one number that could be zero and is not: `takeDamage`
moves the buffer out, so the next frame starts from capacity 0 and pays for the first `push_back` and then for the
growth to two. Keeping the capacity means `takeDamage` copying into its return value instead of moving out of the
member, which moves the allocation rather than removing it — the fix worth making is on the consumer side, where
the frame could hand its own buffer down and get it refilled. Left as a follow-up rather than a one-liner, and
deliberately not done here: `RetainedScene` is the shared surface of several open changes and this is not the one
that should move its ownership model.

### The transform ceiling

The transform payload is the one path with a ceiling instead of a small constant, and the reason is upstream's
parser, reached on purpose. `RetainedScene::parseAnimatedTransform` hands the raw operation array to
`fromRawValue`, the same overload `ViewProps` parsing uses, which is what makes an animated `{"translateX": 20}`
mean exactly what a committed one means (see *Sync props fast path*). `parseProcessedTransform` copies the payload
three times on the way to a `Transform`: into the `RawValue`'s own `folly::dynamic`, into a
`std::vector<RawValue>` of operations, and into a `std::unordered_map<std::string, RawValue>` per operation — each
copy carrying that operation object's node and bucket array — before pushing the parsed `TransformOperation` onto
`Transform::operations` and copying that vector into the result. On the smallest possible payload, a single-axis
translation, that is on the order of twenty allocations per frame, every frame, for a value that changes only in
its magnitude.

**Follow-up, and the one worth doing:** pre-parse transforms into a POD in the animated payload. The shape of an
animated transform is fixed for the life of the animation — the driver knows at `connectAnimatedNodeToView` time
that it is sending a translation — so the per-frame message could carry the operation list as a fixed-size struct
and skip `folly::dynamic` entirely. That is a change to what `animationbackend::packAnimatedProps` sends rather
than to anything on this side, which is why the ceiling is documented here instead of the number being driven
down: the parse is upstream's, and matching it exactly is the point of the equality tests in
`AnimatedPropsTest.cpp`.

### What the perf and e2e half still owes

The `animated-frames.json` p95 gate already exists in the e2e driver — `animated.js` for 240 frames at
`{"p95Ms": 16.7, "minFrames": 60}`, see *E2E driver* for why 16.7 ms and not the gospel's 8.33 ms. That covers
#124's "a simple continuously animating view is in the gate permanently" criterion, which is
[core#50716](https://github.com/facebook/react-native/issues/50716)'s shape. Still open, all of them still #124:

- **N concurrent animated nodes.** #124 asks for a stated N and for predictable degradation past it. Neither the
  scenario nor the number exists; the unit half above says the per-node cost is constant, not what N nodes cost.
- **Animating while a list scrolls.** The
  [core#34583](https://github.com/facebook/react-native/issues/34583)/[core#38470](https://github.com/facebook/react-native/issues/38470)
  combination, which is the shape that freezes rather than merely stutters. Needs a fixture bundle that has both,
  and inherits the animated-fixture problem *Hit-testing under animation* already names.
- **The animation step's own budget, separately from the frame's.** Measured with `wp_presentation` feedback at
  60 Hz and 120 Hz, per #20's harness. Today the driver asserts the frame, not the step inside it.
- **CI artifact trend.** `build/e2e` is uploaded on every run, but nothing reads yesterday's numbers, so a
  regression that stays under the ceiling is invisible. The allocation counts this suite prints have the same gap
  and the same fix.

## Mounting cost (#106)

Upstream shipped a first mounting transaction that allocated about **6.9 GB of raster data** while the JavaScript
bundle was still evaluating, and the crash report blamed the engine
([core#56980](https://github.com/facebook/react-native/issues/56980)); a separate report measured about a
millisecond per view on the new architecture
([core#51869](https://github.com/facebook/react-native/issues/51869)). Neither is visible without a counter, so
`MountingCostTest` in `AllocationCostTest.cpp` is the counter — the same `AllocationProbe.h` the frame-cost gate
uses, because the header permits exactly one includer.

Two shapes of claim, because the two paths have two different costs, and the second is the one worth having:

| What | Measured | Gate |
| --- | --- | --- |
| One mounting transaction, 2000 decorated nodes | 10,023 allocations, **5.01 per node** | ≤ 6 per node |
| One snapshot of those 2000 primitives | **13 allocations, total** | ≤ 32 |
| The same snapshot at 500 versus 2000 nodes | 11 versus 13 | large ≤ 3 × small |
| Mount and unmount 500 nodes, cycle over cycle | 5,522 then **5,522** | exactly equal |

- **The mounting transaction is per node** and always will be: each mutation writes a node into the scene. A
  ceiling per node is what catches a new container per mounted view — the shape of core#56980.
- **The snapshot is not per node**, and asserting a ceiling alone would not prove it: a per-primitive allocation
  hides under any ceiling at a small enough tree. The assertion that a four-times-larger tree does not cost four
  times as much is what actually holds the line, and 11 against 13 is the growth of one vector from empty to 2048
  rather than anything per primitive.
- **The cycle is exact, not bounded.** Mounting and unmounting the same screen twice costs the same number of
  allocations both times, which is issue #106's second item —
  [core#57198](https://github.com/facebook/react-native/issues/57198), memory not reclaimed across repeated
  navigations — as a deterministic count where a resident-set number would be noise.

What this does **not** cover, with the issues that own it: raster memory and peak RSS under the headless
compositor are #20's harness and #76's leak gate, and decoration rasterization keyed by its parameters is a cache
that does not exist yet — there is no raster cache for borders, gradients or shadows to key, because those are
painted straight onto the canvas every frame.

## Upstream animated tests (#132)

We do not write the animated node graph, so we do not write its unit tests either. React Native ships them at
`ReactCommon/react/renderer/animated/tests/`, and that whole path is already inside the `packages/react-native/ReactCommon`
sparse path `scripts/vendor.lock.json` lists for *Core codegen*'s and everything else's sake, so no sparse-path
change was needed to reach it. Verified at `v0.87.1` against `gh api .../animated/tests?ref=v0.87.1`, the tree is
exactly six files: `AnimatedNodeTests.cpp`, `AnimationDriverTests.cpp`, `AnimationTestsBase.h`,
`DecayAnimationDriverTest.cpp`, `EventAnimationDriverTests.cpp` and `ManagedPropsMountingOverrideTests.cpp` — the
same six the vendored checkout already has on disk.

All five `.cpp` files include only `AnimationTestsBase.h` (quoted, resolved relative to their own directory —
no include-path change needed), `<react/renderer/animated/...>`, `<react/renderer/core/ReactRootViewTagGenerator.h>`,
`<react/renderer/graphics/Color.h>` and, for the event-driver test, `<react/renderer/components/scrollview/ScrollEvent.h>`.
Every one of those is already in `RNL_REACT_COMMON_TARGETS` — `react_renderer_animated`, `react_renderer_core`,
`react_renderer_graphics` and `rrc_scrollview` are all linked into `rnl_core_tests` already, from *Animated backend*
and from ScrollView support elsewhere in this file. None of them need `react/test_utils`, `react/renderer/element`
or `rrc_view`/`rrc_root`: `AnimationTestsBase` builds a bare `NativeAnimatedNodesManager` and drives it with
`folly::dynamic` operation payloads, the same shape `AnimatedModule` sends, so nothing here needs a shadow tree,
a `ComponentBuilder`, or a mounted view. That is the same reason `ShadowTreeCommitTest.cpp` avoided
`test_utils::simpleComponentBuilder`, restated for a suite that never touches mounting at all. The five `.cpp` files
therefore compile directly into `rnl_core_tests` — the smaller change over adding subdirectories nothing in this
suite actually calls into. All five build and pass unmodified; none is skipped.

`packages/core/tests/UpstreamAnimatedSuiteTest.cpp` lists the vendored tests directory with `std::filesystem` and
asserts it against the same six-name list above, compiled in through the `RNL_ANIMATED_TESTS_DIR` definition set in
`packages/core/tests/CMakeLists.txt`. An upstream add or remove on the next version bump fails that test instead of
silently changing what this suite covers.

None of the six sources are in `scripts/cpp-coverage.ts`'s `scopedSourcePaths` — that list only ever named our own
`packages/core/src` files, so vendored upstream sources were already outside the 100% gate before this change and
stay there; there is nothing to exclude because there was never anything to include. *Pins*' re-vendoring paragraph
names this suite as a bump gate: a version bump that changes the tests directory's file list is a build-time
`UpstreamAnimatedSuiteTest` failure, and a version bump that changes what the five suites assert is a CTest failure,
both before either reaches an app.

## Layout conformance suite (#118)

`packages/core/tests/LayoutConformanceTest.cpp` is the harness the layout conformance issues hang their tables on.
Every case commits a real tree through `ShadowTree` — the same Yoga layout the platform runs on the commit thread —
and reads absolute frames back out of the committed revision, so a case fails exactly when our pipeline would lay
the tree out differently from the rule the case states. Nothing talks to Yoga directly; props are parsed through
`ViewComponentDescriptor::cloneProps` from the same top-level style keys the JavaScript bundles send, and the
ScrollView cases reuse the production content-size state path. Cases are named after the upstream reports they pin:
#48527 (`alignItems: 'flex-end'` must not change where `flexWrap` breaks lines), #49984 (`alignContent: 'stretch'`
distributes leftover cross space across lines), #35351 (gap participates in available space, with `alignContent`)
and #36024 (gap sizing inside a `ScrollView` matches the same container outside one). The golden for this suite is
`test-bundles/wrapping.js`: one tile grid rendered at two container widths so a break-point regression is a visible
image difference, not a number in a table.

React Native's default flex direction is `column`, not CSS's `row`, so every wrapping case states `row` explicitly —
the questions these cases ask are about horizontal line breaking.

Known deviation from CSS, pinned as observed rather than "fixed":

- **Percentage gaps with an indefinite dimension resolve circularly.** CSS Box Alignment treats a percentage
  `row-gap`/`column-gap` as zero when the corresponding dimension is indefinite. Yoga instead resolves it against
  the resulting content height — a `row-gap: 25%` on a two-line container whose content height comes out at 40
  points becomes a 10-point gap — and then lets the shifted line overflow the container instead of re-including it
  in the measured height. `PercentageRowGapWithIndefiniteHeightResolvesAgainstTheContentHeightLikeYogaNotCss` pins
  this; if a Yoga bump changes it, that test is the tripwire, and the definite-dimension case
  (`PercentageGapsResolveAgainstTheContainerDimensionTheyAreDeclaredOn`) states the correct-dimension rule: percent
  `column-gap` resolves against width, percent `row-gap` against height.

## aspectRatio constraint ordering (#117)

`aspectRatio` is Yoga's constraint solver, and the conformance table
(`CoreIssue57304AspectRatioClampsAndReDerivesThroughTheRatio`) pins the order it actually applies, which is not the
order its consumers assume in the upstream reports:

- The **width anchors**. A definite height does not survive a defined ratio: Yoga re-derives the height as
  width / ratio even when the height was itself definite, so `width: 100, height: 40, aspectRatio: 2` lays out
  100x50.
- Min/max on the anchoring axis clamp **before** the derivation and the clamp re-derives the other axis through the
  ratio: `height: 60, aspectRatio: 2, maxWidth: 100` lays out 100x50, not 100x60 — the width clamp feeds forward.
- Min/max on the derived axis clamp **without** propagating back: `width: 100, aspectRatio: 2, maxHeight: 30` lays
  out 100x30, and the width never learns about it. Contradictory constraints on the derived axis resolve to the
  min, as CSS says.
- **Contradictory constraints on the anchoring axis keep the min but derive the height from the max**:
  `height: 60, aspectRatio: 2, minWidth: 150, maxWidth: 100` lays out 150x50 — the width honours `minWidth`, but
  the height was already derived from the max-clamped 100, so the ratio does not hold between the final axes. CSS
  would re-derive 75. This is the one place the table pins a genuine Yoga inconsistency; a Yoga bump that changes
  it fails `heightAndRatioContradictoryWidthConstraintsKeepTheMinButTheHeightFollowsTheMax`.
- Degenerate ratios — zero, negative, NaN, infinity — default to auto at the parse boundary
  (`CoreIssue35858DegenerateAspectRatioValuesDefaultToAuto`). Zero and infinity are normalised by `Style::setAspectRatio`
  itself, NaN is Yoga's undefined, and a negative ratio falls out of the derivation and bounds to zero; none of
  them reaches the layout engine as a usable number.

The golden for this table is `test-bundles/aspect-ratio.js`: one image at width + ratio, and the same image clamped
by `maxWidth`, side by side.

## Native-driver allowlist (#122)

*Sync props fast path* explains why `opacity`, `backgroundColor` and `transform` are the three props an animation
frame can change without a Fabric commit. This section is about keeping that number honest: what the fast path
applies, what the JavaScript side advertises, and what a user is told when the two do not meet.

### One table

`packages/core/src/AnimatedPropAllowlist.h` is the single definition. `AnimatableProp` is the enumeration,
`kAnimatableProps` is a `constexpr std::array` of name-to-enumerator entries, and `animatablePropFor` is the
lookup. `RetainedScene::applyAnimatedProps` no longer compares strings: it calls `animatablePropFor`, pushes a
`RejectedAnimatedProp` when the lookup misses, and otherwise switches over the enumerator. That switch is what
makes the table load-bearing in both directions — an entry added to `kAnimatableProps` without a `case` is a
`-Wswitch` warning on a build that already runs `-Wall -Wextra`, and paint code added without an entry is
unreachable, because the lookup that gates it would still miss.

### The oracle, and drift in both directions

`ReactCommon/react/renderer/animated/internal/NativeAnimatedAllowlist.h` is upstream's
`getDirectManipulationAllowlist()`: the thirty-three style keys `NativeAnimatedAllowlist.js` advertises as direct
manipulation eligible, vendored as C++ rather than JavaScript, so nothing here parses anything. It is the same
function `StyleAnimatedNode` uses to decide whether an update is a layout update, which is what decides whether a
frame reaches us through the fast path at all — so it is the oracle by construction rather than by convention.

`packages/core/tests/AnimatedPropAllowlistTest.cpp` holds the two lists against each other:

- `EveryPropWePaintIsOneTheJavaScriptSideAdvertises` — every name in `kAnimatableProps` is in upstream's set. We
  never claim more than JavaScript offers, and an upstream removal on a version bump fails here.
- `EveryAdvertisedPropWeDoNotPaintIsAnEnumeratedDifference` — the thirty upstream names outside `kAnimatableProps`,
  sorted, against a written-out list. Painting a new prop means deleting its line; an upstream addition on a
  version bump appears in the difference and fails here.
- `TheTableIsWhatDecidesWhetherTheFastPathAppliesAProp` — `animatablePropFor` answers for each of the three and
  misses for a prop that is only advertised.

The thirty are the ten border and shadow colours minus `backgroundColor`, the thirteen border radii, `color`,
`tintColor`, `elevation`, `zIndex`, `shadowOpacity`, `shadowRadius` and the four legacy Android transform
operations. None is a refusal in principle. Border colours and radii live inside `BorderMetrics`, which
`resolveBorderMetrics` cascades out of nine props, so animating a single edge is a re-cascade rather than a field
write; it belongs with whoever needs it first.

### The diagnostic

`rejectedAnimatedPropMessage` in `LinuxMountingManager.cpp` composes the line, joining the supported set out of
`kAnimatableProps` so the message cannot name a set the code does not implement. It is a free function of its
arguments, so the assertion on the exact text is a unit test rather than a log-capture rig, and it is inside the
100% gate because `LinuxMountingManager.cpp` is. The two shapes, asserted verbatim:

```text
[mounting] synchronous update carries prop shadowRadius, which the Linux native driver cannot animate; supported:
opacity, backgroundColor, transform — animate it with useNativeDriver: false or file an issue
```

```text
[mounting] synchronous update carries prop opacity with a non-finite value, which the Linux native driver cannot
paint; supported: opacity, backgroundColor, transform — check the interpolation output range or animate it with
useNativeDriver: false
```

The counting policy is unchanged: every rejection increments `MountDiagnostics::rejectedAnimatedProps`, the first
name is kept in `firstRejectedAnimatedProp`, only the first is logged, and the rest of the payload still applies.

### The non-finite boundary

The second shape exists because `std::clamp` propagates `NaN` through both of its comparisons, so a `NaN` opacity
would have reached `toArgb`, multiplied every alpha in the subtree and been rounded by `std::lround` into an
unspecified value. `applyAnimatedProps` now refuses any allowlisted prop whose payload is a non-finite double —
issue #73's boundary rule applied to animation — and counts it like any other rejection.
`ANonFiniteOpacityFromTheRealNodesManagerIsRejectedAndCounted` drives it through a real
`NativeAnimatedNodesManager`: a value node holding `NaN`, a style node, a props node connected to a mounted view,
one `onRender`, and the assertion that the scene still paints the colour it mounted with. A `NaN` inside a
`transform` operation array is not covered by this check — the guard reads the payload value, and a transform
arrives as an array — and is a follow-up.

### What upstream's suites cover, and what they do not

Items 2 to 4 of #122 are interpolation and node-graph behaviour, which is upstream's code and, where it is tested
at all, upstream's tests. Read against `react/renderer/animated/nodes`, the five suites compiled in by #132 cover:

- **Node types:** `value` (including `setOffset` idempotence), `color`, `style`, `props`, `modulus`, `diffclamp`,
  `round` and `object`. `AnimatedNodeTests` builds an RGBA colour graph and an opacity graph and asserts the props
  that reach the mounting callback.
- **Drivers:** `frames` (`AnimationDriverTests`, including reconfiguration and deferred start) and `decay`
  (`DecayAnimationDriverTest`, five cases against the analytical curve).
- **Events:** `EventAnimationDriverTests` maps a `ScrollEvent`'s `contentOffset.y` and `zoomScale` onto two value
  nodes through `getEventEmitterListener`.
- **Managed props:** `ManagedPropsMountingOverrideTests` covers the live-value mirror across frames, on connect,
  mid-flight, and per view.

Nothing upstream covers, and therefore nothing this platform's animation stack has a test for:

- **Interpolation, entirely.** No suite creates an `interpolation` node. That means no numeric output range, no
  `extrapolateLeft`/`extrapolateRight` (`identity`, `clamp`, `extend`), no `easingStops`, no degenerate
  `inputMin == inputMax` range, and no non-finite input — the whole of #122 item 2.
- **Colour interpolation** (`outputType: "color"` and `"platform_color"`), which is the crash path of core#34022.
- **Unit-carrying output ranges** (`deg`, `rad`, `%`), which is core#36608. `InterpolationAnimatedNode` reads a
  non-colour output range with `asDouble()`, so a unit suffix is a `folly` conversion rather than a modelled type.
- **The remaining node types:** `interpolation`, `addition`, `subtraction`, `multiplication`, `division`,
  `transform` and `tracking`. `transform` is exercised end to end by our own `AnimatedHitTestTest` and
  `AnimatedPropsTest` rather than by a node unit test; the operators and `tracking` are untouched. `division` by
  zero is the one that produces a non-finite value with no guard anywhere upstream, which is what the boundary
  check above exists for.
- **Value listeners.** `startListeningToAnimatedNodeValue` has no coverage, so core#37061 and core#49719 —
  listeners never firing on operator-built values — are unverified here.
- **The spring driver.** `SpringAnimationDriver` has no suite at all.

Writing those tests is upstream work; the entry cost is `AnimationTestsBase`, which is already compiled into
`rnl_core_tests`, so an interpolation suite is a new `.cpp` in the vendored tests directory and an
`UpstreamAnimatedSuiteTest` list update, not new plumbing.

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
pnpm codegen                                        # node scripts/codegen-core.ts — see *Core codegen*
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
pnpm --filter @react-native-linux/core run:animated-scroll  # hello_react --animated-scroll animated-scroll.js 160 100 3
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
node scripts/codegen-core.ts
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
and a surface coordinate, clicks there, and prints whatever the bundle prints; see *Input*. `--animated-scroll`
takes the bundle path, a surface coordinate and a wheel notch count, drives the whole glide one frame at a time
with the beat induced per frame, and prints whatever the bundle prints on each of them; see *Event-driven
Animated*. `--resize` takes the
bundle path, a width and a height, runs the bundle at the headless surface size and then resizes the surface to
them, so the bundle can print its boot constants and the `didUpdateDimensions` that follows; see *Dimensions and
TurboModules*. `--ime-debug` takes no
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
| `borderRadius` and the per-corner props | `BaseViewProps::resolveBorderMetrics` — the same call iOS and Android make — cascades the corners, resolves percentages against the frame, and applies the CSS corner-overlap clamp. `roundedBorderBox` turns the result into the one `SceneRoundedBox` below, and the painter turns that into an `SkRRect` with per-corner x/y radii. |
| `borderWidth` and the per-side props | Read from the Yoga style through `resolveBorderMetrics`, then promoted to at least one device pixel by `visibleBorderWidth`, drawn **inside** the frame as the ring between the border box and the content box `roundedContentBox` leaves inside it (`drawDRRect`). |
| `borderColor` and the per-side props | One `drawDRRect` when the four colours are equal; otherwise one mitred wedge clip per side, overlapping its neighbours by one point, so each side keeps its own colour, each corner is split on the diagonal and no background survives between two sides. |
| `overflow: hidden` | `getClipsContentToBounds` pushes the node's rounded border box onto a clip stack that descendant primitives carry; the painter replays it with `clipRRect` under the clipping ancestor's own transform, and the hit test replays it with `roundedBoxContainsPoint`. |
| `transform` | `BaseViewProps::resolveTransform` resolves the operation list and folds in `transformOrigin`; the scene applies it about the centre of the absolute frame, composes it with every ancestor transform, and reduces the 4x4 to a 2D affine the painter hands to `SkMatrix`. |
| `zIndex` | Nothing to implement. Fabric sets `ShadowNode::orderIndex_` from `zIndex` in `ConcreteViewShadowNode` and stable-sorts siblings by it in `sliceChildShadowNodeViewPairs` before diffing, so mount order — which is the scene's child order and therefore paint order — is already `zIndex` order. |

### One rounded box (#99)

Every consumer of a node's shape derives from one value. `RetainedScene.h` declares it:

```text
SceneRoundedBox roundedBorderBox(frame, radii)          the node's own rounded border box
SceneRoundedBox roundedContentBox(borderBox, widths)    the box a ring of those widths leaves inside it
bool            roundedBoxContainsPoint(box, point)     the containment every hit test asks
```

There are six consumers and none of them computes a rounded rect of its own:

| Consumer | Where | Which box |
| --- | --- | --- |
| background fill | `ScenePainter::paintPrimitive` | `roundedBorderBox` |
| gradient layers | `ScenePainter::paintBackgroundImages` | the same value, passed in |
| `<Image>` content clip | `ScenePainter::paintImage` | the same value, passed in |
| border ring | `ScenePainter::paintBorder` | `roundedBorderBox` and `roundedContentBox` of it |
| focus ring | `ScenePainter::paintFocusRing` | `roundedContentBox` of it, inset by half the stroke |
| `overflow: hidden` clip | `ScenePainter::paintScene` and `RetainedScene::coversPrimitive` | `roundedBorderBox` of the clip |
| hit region | `RetainedScene::coversPrimitive` | `roundedBorderBox` of the primitive |

The type carries `facebook::react::Rect` and `BorderRadii` and no Skia type, because the hit test is one of those
consumers and `RetainedScene.cpp` links no Skia. `roundedBoxContainsPoint` tests the bounds and then each corner's
ellipse, scaled by `(horizontal * vertical)^2` so there is no division and a zero radius needs no case of its own.

The consequence that is a behaviour change: **a press outside a rounded corner's arc misses.** Upstream's
shadow-tree hit test presses the bounding rectangle; see *Hit-testing under animation* for why this one does not.

### Dashed and dotted borders (#101)

A dashed ring is **one stroke down the middle of the band**, with `SkDashPathEffect` on it, rather than four
dashed sides. That is the whole reason it looks right: the dash phase runs continuously around the corner arcs,
where a hand-rolled per-side implementation restarts it four times and produces the corners
[core#32918](https://github.com/facebook/react-native/issues/32918) has been open about since 2022. `drawDRRect`
cannot do it — it fills the difference between two rounded rects and has no path to dash along — so this is the
one place the ring is a stroke instead of a fill.

- The pattern is in **multiples of the border width**, not absolute points: a dash is three widths on and two
  off, and a dot is a zero-length on-interval under a round cap. A four-point dashed border and a one-point one
  are then the same border at two sizes, which is what an author means by `dashed`.
- **Per-side colours still work**, through the same wedge clips the solid ring uses. Each side is a clipped
  portion of the *same* stroked path, so a four-colour dashed ring keeps one phase rather than four.
- **Four different widths fall back to solid.** A stroke has one width, and a ring with four is four strokes that
  no dash phase can be continuous across. That is the deviation, and it is this narrow: uniform width, any
  radius, any colours, dashes.
- **The background is untouched by any of it.** The fill is painted before the ring and the ring is a stroke over
  it, so [core#42289](https://github.com/facebook/react-native/issues/42289) and
  [core#45368](https://github.com/facebook/react-native/issues/45368) — a dashed or dotted border taking the
  background down with it — cannot happen here. `NoBorderStyleAtAnyWidthChangesWhatIsPaintedBehindTheRing` asserts
  the whole table: three styles by three widths by an opaque and a translucent background.

The picture is the sixth row of `border-matrix.png`, which is why that golden is 700 points tall rather than 600.

### Border painting (#100)

- **Per-side colours are mitred wedge clips over one `drawDRRect`**, not four filled trapezoids. The ring is
  where two rounded rects differ and Skia has that primitive; building each side as a path would re-derive the
  outer arc, the inner arc and the CSS inner-radius rule by hand, next to a `SceneRoundedBox` that already
  carries all three. Four equal colours skip the clips entirely and draw the whole ring at once.
- **The wedges overlap by one point.** Two anti-aliased wedges that merely abut leave the diagonal half covered
  by each, and compositing them one after the other leaves a quarter of the background showing — the seam
  [core#33950](https://github.com/facebook/react-native/issues/33950) reports and this renderer had. Overlapping
  them means the later side is drawn at full coverage over the earlier one, so a mitre is a colour boundary
  rather than a gap. The cost is that the diagonal is displaced by up to one point toward whichever side is
  painted later; the order is top, right, bottom, left, so each corner is won by a different side. The
  overlap is why `view-props.png` was regenerated with this change: 144 of its 480,000 pixels differ, all of them
  on the four mitre diagonals of its per-side-widths tile, where the background used to show through.
- **A sub-pixel width is promoted to one device pixel** by `visibleBorderWidth`, against
  `LayoutMetrics::pointScaleFactor`, which is what `StyleSheet.hairlineWidth` needs and what
  [core#58054](https://github.com/facebook/react-native/issues/58054) reports missing. A width of exactly zero
  stays zero: "no border" is not a thin one. The promotion is in the scene rather than the painter so the ring,
  the content box it leaves behind and the coverage gate all read the same number.
- **A transparent side removes only itself.** Each side is drawn independently, so the
  [core#34722](https://github.com/facebook/react-native/issues/34722) failure — one transparent side erasing all
  four — cannot happen here.
- **A translucent ring composites against what is under it.** The ring is drawn straight onto the canvas with no
  intervening layer, which is [core#39286](https://github.com/facebook/react-native/issues/39286), and a radius
  changes nothing about that, which is
  [core#49606](https://github.com/facebook/react-native/issues/49606).

Known deviations from iOS and Android, all deliberate:

- **Opacity is per-primitive, not group opacity.** React Native composites a translucent subtree as one layer; we
  multiply the alpha of each primitive instead. Overlapping descendants of a translucent ancestor therefore blend
  against each other where the real thing would not. Fixing it means `saveLayerAlphaf` per stacking context.
- **`borderStyle` draws, and a ring with four different widths is the one case that does not** (#101). See
  *Dashed and dotted borders* below.
- **`borderCurve` is ignored.** Corners are always circular, never iOS' `continuous` squircle.
- **An unset `borderColor` draws nothing, and cannot be told from a transparent one.** Upstream's own cxx
  `HostPlatformColor` is `using Color = int32_t` with `UndefinedColor = 0`, and `transparent` processes to
  `rgba(0, 0, 0, 0)`, which is also 0. Unset and transparent are therefore the same value in the type
  `resolveBorderMetrics` returns, so falling back to black for the unset case — which is what iOS and Android
  both do — would black out every explicitly transparent edge instead. That is
  [core#34722](https://github.com/facebook/react-native/issues/34722) turned inside out, and the literal reading
  is taken instead: a border with no colour draws nothing. Distinguishing the two needs a change to upstream's
  colour type, not to this renderer.
- **`overflow: hidden` clips to the border box**, matching iOS `clipsToBounds`, where CSS clips to the padding box.
- **Transforms are 2D only.** The 4x4 is reduced to its affine part, so `perspective`, `rotateX` and `rotateY` lose
  their depth and collapse to their in-plane component, and `backfaceVisibility` is not honoured.

The proof is `packages/core/tests/BorderGeometryTest.cpp`, inside the 100% gate — the content-box arithmetic, the
corner containment, the hairline promotion, and the identity between what the hit test answers and what
`roundedBorderBox` says was painted, sampled over a grid — plus the goldens `rounded-box.png` and
`border-matrix.png`; see *Golden images*. The third layer is the `rounded-press.json` e2e scenario, which presses
a real card through a compositor's `wl_pointer` and watches the hit flip at the arc; see *The scenarios*.

Not implemented at all, and owned elsewhere: `elevation`, `filter`, `mixBlendMode` and `isolation`, and
`outline*`. Each needs its own issue under M1; none of them is a variation on what is here.
`experimental_backgroundImage` gradients and `boxShadow` with the legacy `shadow*` quartet were on that list and
are now implemented; see *Gradients* and *Shadows* below.

## Gradients

`experimental_backgroundImage` — CSS `linear-gradient()` and `radial-gradient()` as a `<View>` background. This is
issue #34, and almost all of it is upstream's: React Native already parses the prop into
`std::vector<facebook::react::BackgroundImage>`, a `std::variant<LinearGradient, RadialGradient>`, on
`BaseViewProps::backgroundImage`. Nothing in this repository re-models those types.

### The pipeline, end to end

```text
experimental_backgroundImage           a CSS string, or the list processBackgroundImage produces
  → BackgroundImagePropsConversions    upstream: LinearGradient / RadialGradient with ColorStop lists
  → RetainedScene::writeNode           copied onto SceneNode::backgroundImage, unresolved
  → SceneSnapshot                      ScenePrimitive::backgroundImage + backgroundImageOpacity
  → makeGradientShader                 src/GradientShader.cpp: the CSS geometry, one SkShader per layer
  → ScenePainter::paintBackgroundImages  filled into the same rounded border box the solid colour uses
```

The stops are **not** resolved in the scene, which is the one place this feature departs from the rule that
`RetainedScene` composes everything and `ScenePainter` only issues Skia calls. Resolving them needs the CSS
gradient line, and the gradient line needs the box being filled — so the arithmetic would have to be redone in the
painter anyway. Opacity is the visible consequence: every other colour a snapshot carries has the inherited
opacity multiplied into its alpha, and a gradient cannot, because it has an unbounded number of colours. It
travels as `ScenePrimitive::backgroundImageOpacity` and becomes the paint alpha, exactly as an untinted `<Image>`
already does.

A node whose only visible content is a gradient is painted and damaged like any other: `isPrimitiveVisible` counts
a non-empty layer list, so a `<View>` with no `backgroundColor` and no border still emits a primitive, and the
damage rules need no term of their own because a gradient is painted inside the frame.

### Paint order

CSS paints the **first** background image nearest the viewer, so the last entry in the list is bottom-most. The
painter therefore walks `ScenePrimitive::backgroundImage` back to front, which is what React Native's own Android
`BackgroundImageDrawable::draw` does — "iterate in reverse to match CSS spec i.e first background image appears
closer to user". A solid `backgroundColor` stays underneath all of them, and an `<Image>`'s pixels stay above.

### The formulas

All of them are ports of React Native's Android implementation
(`com.facebook.react.uimanager.style.{LinearGradient,RadialGradient,ColorStopUtils}`), which is itself a port of
Blink's `css_gradient_value.cc`. Porting rather than inventing is the point: a gradient that differs from Android
by a few degrees is a bug nobody can see until a designer notices.

- **Colour-stop fix-up** (`fixedColorStops`) is the CSS algorithm from
  [css-images-4](https://drafts.csswg.org/css-images-4/#coloring-gradient-line): an unpositioned first stop sits at
  0 and an unpositioned last stop at 1; a percentage is a fraction of the gradient line and a length is `px /
  gradient-line-length`; a position never moves backwards past the largest one before it; each run of unpositioned
  stops is spread evenly between the positioned stops around it. `ProcessedColorStop` is upstream's own struct.
- **Linear direction.** An angle arrives as a `Float` in CSS degrees — `to right` and the other three axis
  keywords are already turned into 0/90/180/270 by the JavaScript side. The four *corner* keywords arrive as
  `GradientKeyword` and become `atan(width / height)` offsets from the nearest axis, so the line is perpendicular
  to the box's other diagonal.
- **Linear gradient line.** Centred on the box, at that angle, ending where the perpendicular through the corner
  the angle points at crosses it. The four axis-aligned angles are returned directly, because the perpendicular
  slope is infinite there.
- **Radial position.** `left`/`top` resolve against width/height through upstream's `ValueUnit::resolve`;
  `right`/`bottom` resolve the same way and are subtracted from the box; an unset axis is the centre.
- **Radial size.** `closest-side`/`farthest-side` are the distances to the nearest or farthest edge on each axis,
  collapsed to one radius for a circle. `closest-corner`/`farthest-corner` take the ellipse through that corner
  with the aspect ratio of the same-named side-sized ellipse, per
  [css-images-3](https://www.w3.org/TR/css-images-3/#typedef-radial-size). Explicit lengths resolve directly.
- **Ellipses** are `SkShaders::RadialGradient` of the horizontal radius with a local matrix scaling y by
  `ry / rx` about the centre — one matrix rather than a second shader type, again matching Android.

### Shadows (#67)

`boxShadow` and the legacy iOS quartet — `shadowColor`, `shadowOffset`, `shadowOpacity`, `shadowRadius` — are
one drawing. `resolveShadows` in `RetainedScene.cpp` turns both into a list of `SceneShadow` in paint order: the
`boxShadow` entries first, as authored, then the quartet as one more outset shadow when it says anything at all.
The painter only ever sees the list.

- **The quartet maps onto `boxShadow` with the radius doubled.** iOS' `shadowRadius` is the Gaussian sigma;
  CSS' blur radius is the full blur width, two sigmas
  ([css-backgrounds-3](https://www.w3.org/TR/css-backgrounds-3/#shadow-blur)). `kSigmasPerBlurRadius` is that
  factor, and `shadowOpacity` is multiplied into the colour's alpha, so a design specified for iOS casts the same
  shadow here. A quartet whose `shadowOpacity` is at its default of zero, or whose colour is not meaningful,
  casts nothing — the same as iOS, and the reason a `<View>` with a stray `shadowColor` does not grow a halo.
- **Sigma is half the blur radius on the way back out.** `toBlur` is `SkMaskFilter::MakeBlur(kNormal, blur / 2)`,
  and no filter at all for a blur of zero, so a sharp shadow is a plain rounded rect rather than a blur with a
  degenerate sigma. Android's `CSSBackgroundDrawable` and the iOS `RCTBoxShadow` layer make the same choice.
- **The list is drawn back to front.** CSS paints the first shadow written on top of the ones after it, so both
  passes iterate the list in reverse — and because the legacy quartet is appended after the `boxShadow` entries,
  it lands underneath them, which is the order a component that sets both wants.
- **Outset shadows are painted before the fill, under a difference clip.** The canvas clips out the node's own
  rounded border box (`SkClipOp::kDifference`) and draws each shadow as the border box outset by
  `spreadDistance` and translated by the offset (`spreadAndOffset`). The clip is what keeps a translucent shadow
  from darkening the node's own background where the blur reaches back under it — CSS says the shadow is not
  drawn inside the border box, and a translucent background makes the difference visible.
- **Inset shadows are an even-odd ring drawn after the background image.** The clip is the border box; the path
  is a rect `kInsetShadowMargin` points larger on every side with the border box, spread inwards and offset,
  as its hole, filled even-odd so only the band between them takes the blur. The margin only has to exceed the
  widest blur a shadow can spread into the box, so it is a constant rather than a computation. Inset shadows sit
  above the background and below the border, which is the CSS stacking order.
- **The shadow damages where it reaches, which is further than its blur radius.** `shadowExtent` grows a
  primitive's frame by `spreadDistance + 1.5 * blurRadius` on every side, and by the offset on the two sides the
  shadow moves towards, over every outset shadow it carries; inset shadows add nothing. The one-and-a-half is the
  painter's own arithmetic read back: sigma is half the blur radius, and Skia's blur support is three sigmas
  (`SkBlurEngine` sizes its kernel at `ceil(3 * sigma)`), so pixels appear up to one and a half radii out and
  damage that stopped at the radius would leave the outermost of them stale. `primitiveDamageBounds` maps that grown rect through the node's
  matrix, so a shadowed card that moves repaints the strip its shadow vacated — the damage golden would show a
  ghost otherwise, and `ShadowDamageTest` asserts the extent rather than the picture.
- **`elevation`** is Android's shorthand for a material shadow, and the shared `ViewProps` does not carry it —
  `react/renderer/components/view/ViewProps.h` has no such field; Android reads it from the raw props on its own
  side — so there is nothing to map. It stays on the *not implemented* list above.

Three layers, as everywhere. `ShadowTest.cpp` covers the resolution (paint order, the quartet's doubling and
opacity, the two ways a quartet casts nothing, inherited opacity folding into the colour) and the extent (no
shadow, one shadow, an offset larger than the reach never pulling the extent inside the box, several shadows
reaching as far as the furthest on each side, inset reaching nowhere) — and the damage of a shadow *appearing*
and *going away*, each read after the mount's own full-surface damage has been drained, because that damage
would otherwise cover the extent whether or not `primitiveDamageBounds` knew about shadows; `shadow.png` is the golden of
`shadow.js`, one tile for each way a shadow interacts with the node: outset, inset, two in paint order,
per-corner radii, under an `overflow: hidden` ancestor, on a rotated node, the legacy quartet, and spread with no
blur. The e2e layer is not a separate scenario: a shadow neither receives input nor emits an event, and the
window golden of the same fixture would only re-prove the raster one.

Two things the tiles are there to catch, because both were wrong once during the work. The clipped tile's shadow
is cut where the *ancestor* cuts and nowhere else — a shadow clipped by its own node's box is the difference-clip
mistake in the other direction. The rotated tile's shadow rotates with the box: the shadow is drawn in the node's
matrix, not offset in screen space after the fact.

### An unresolvable fontFamily says so (#70)

The default font manager is **fontconfig**, and fontconfig substitutes rather than failing. So a `fontFamily`
nobody registered does not fail to resolve — it resolves to whatever the system had, and the text draws in the
wrong face with no error anywhere. That is react-native-windows
[#16308](https://github.com/microsoft/react-native-windows/issues/16308) exactly: a registered icon font that
renders blank glyphs, reported as "silent failure in the DWrite font path".

Asking whether a name resolved is therefore the wrong question, because every name resolves. The right one is
whether the face that came back **is the family that was asked for**, and `toFontFamilies` compares the resolved
typeface's own family name to the request:

```text
[text] fontFamily "Totally Missing Icons" is not registered; drawing "Liberation Sans" instead
```

Three properties of that line, each deliberate:

- **The text still draws.** Falling back is the right behaviour and refusing to draw would be worse; what was
  missing is the sentence, not the glyphs.
- **It names the substitute**, because "not registered" alone does not tell an author whether the asset is missing
  or the name is misspelled, and the face that was used does.
- **Once per family name, not once per paragraph or per frame.** A missing family is missing on every frame, and a
  line per frame is a line nobody reads.

The CSS generic families — `serif`, `sans-serif`, `monospace`, `cursive`, `fantasy` — are exempt, because
resolving `monospace` to a real monospace face is the point of asking for it rather than a failure to find it.

### Fidelity limits

- **Transition hints are not interpolated.** `linear-gradient(red, 20%, blue)` is a stop whose colour is absent,
  and upstream's C++ `ColorStop` models an absent colour as `SharedColor{0}` — which is also what
  `transparent` processes to. The two are indistinguishable in that type, so the literal reading is taken:
  `transparent` stops are correct, and a transition hint paints a transparent-black stop instead of the nine
  interpolated stops browsers and Android insert.
- **Colours are interpolated unpremultiplied, in the destination colour space.** That is Skia's default and what
  Android gets from `android.graphics.LinearGradient`; the web premultiplies, so a ramp to `transparent` darkens
  differently here than in a browser. `interpolation` hints (`in oklch`, hue methods) are not supported at all —
  React Native does not parse them.
- **`repeating-linear-gradient` and `repeating-radial-gradient` are not supported**, because React Native does not
  parse them either. Every gradient is `SkTileMode::kClamp`.
- **`background-size`, `background-position` and `background-repeat` are ignored.** Each layer fills the whole
  border box. Upstream parses those props into `BaseViewProps`; honouring them is a separate issue.
- **The layer is clipped to the border box**, matching the solid `backgroundColor`, where CSS would paint a
  background image into the padding box by default.
- **The CSS string form needs a feature flag.** `fromRawValue` only routes a string through the native CSS parser
  when `ReactNativeFeatureFlags::enableNativeCSSParsing()` is on, and it is off in this build. A bundle that talks
  to `nativeFabricUIManager` directly must therefore send the list shape `processBackgroundImage` produces, which
  is what a real app sends anyway. `packages/core/test-bundles/gradient.js` does exactly that.
- **A radial layer must carry `size` for its `position` to be read**, because upstream's
  `parseProcessedBackgroundImage` nests the position branch inside the size branch, and must carry `shape`,
  because `RadialGradient::shape` has no default member initialiser. Both are always present in what
  `processBackgroundImage` emits.

The golden is `packages/core/goldens/gradient.png`, from `packages/core/test-bundles/gradient.js`; see
*Golden images*. The extraction half — the layers reaching the primitive, the opacity travelling beside them, and
a gradient alone being enough to paint and damage a node — is in `packages/core/tests/SceneTest.cpp`, inside the
100% gate. `GradientShader.cpp` is not in the gate, for the same reason `ScenePainter.cpp` is not: it produces
pixels, and the golden is what reads them.

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
| `lineHeight` | Points converted to Skia's multiple-of-font-size `height`, with half leading, which is what CSS and both React Native platforms do. See *Vertical metrics (#110)*. |
| `letterSpacing` | `TextStyle::setLetterSpacing`. |
| `textAlign` | `left`, `right`, `center`, `justify`, `start`/`end`, and `auto` as `start`. |
| `textDecorationLine` | `underline`, `line-through` and both together; `textDecorationColor` falls back to the foreground colour. |
| `numberOfLines` | `ParagraphStyle::setMaxLines`. |
| `ellipsizeMode` | Anything but `clip` sets a `…` ellipsis. |
| Inline attachments | Added as SkParagraph placeholders sized from the attachment's own measured frame, and reported back through `getRectsForPlaceholders`. |

### Vertical metrics (#110)

React Native's `lineHeight` is an absolute point value and SkParagraph's `TextStyle::height` is a multiple of the
font size, so the ratio is what crosses the boundary. The arithmetic on both sides of it is
`packages/core/src/LineBoxMetrics.h` — `lineHeightRatio` for the value SkParagraph takes, `measureLineBox` for
where the line actually lands — and it is pure, so it is under the 100% gate rather than under a rasteriser.

**The policy, for a font reporting ascent `A` and descent `D` at the resolved font size:**

- With no `lineHeight`, the line box is the font's own `A + D`. The font decides; nothing is added or removed.
- With a `lineHeight`, **the line box is exactly `lineHeight`**, and the difference `lineHeight - (A + D)` is split
  evenly above and below the glyphs. Half leading is `(lineHeight - (A + D)) / 2` and the baseline sits at
  `halfLeading + A` from the top of the box. react-native#39145 is what applying that leading to one side looks
  like; we do not do it.
- When `lineHeight` is smaller than `A + D` — which includes `lineHeight == fontSize`, since every real text font
  has `A + D > fontSize` — **half leading is negative and the glyphs overflow their line box**. They are not
  scaled down, the box is not grown, and the paragraph does not clip them: `ScenePainter::paintText` sets no clip,
  so ascenders and descenders draw outside the measured frame exactly as CSS `line-height` and both React Native
  platforms draw them. That is the deliberate answer to react-native#49886 and #53286. The only clip over text is
  a `<TextInput>`'s own content box, which is horizontal scrolling and predates this.
- **Measure and paint agree by construction.** Both go through the one `layoutParagraph`, so the height
  `TextLayoutManager::measure` hands Yoga is the height of the same line boxes the painter fills. A parent `<View>`
  with `overflow: hidden` still clips, because that is the view's rule, not the paragraph's.
- **First and last lines follow the same rule as interior lines.** `ParagraphStyle::setTextHeightBehavior` is set
  explicitly to `TextHeightBehavior::kAll`, so the first line's ascent and the last line's descent stay inside the
  applied height. `kDisableFirstAscent`/`kDisableLastDescent` would trim the leading off the top and bottom of the
  paragraph — the asymmetry upstream's platforms disagree about — and would make a one-line paragraph measure
  differently from the same line in the middle of a longer one. `kAll` is also SkParagraph's default; it is set at
  the call site so the choice is a line of code rather than an inherited default.

The SkParagraph settings that add up to that are `TextStyle::setHeight(lineHeight / fontSize)`,
`TextStyle::setHeightOverride(true)`, `TextStyle::setHalfLeading(true)` per fragment, and
`ParagraphStyle::setTextHeightBehavior(kAll)` per paragraph. `lineHeight` is ignored — no override at all — when
it is absent, which React Native spells NaN, or when the resolved font size is not positive, because a ratio
against zero is not a number SkParagraph can use.

With the synthetic font `packages/core/tests/LineBoxMetricsTest.cpp` asserts against, `A = 22` and `D = 6`, so the
natural line box is 28 points at a font size of 20:

| fontSize | lineHeight | Line box | Half leading | Baseline from top |
| --- | --- | --- | --- | --- |
| 20 | absent | 28 | 0 | 22 |
| 20 | 20 (`== fontSize`) | 20 | -4 | 18 |
| 20 | 14 (`0.7 * fontSize`) | 14 | -7 | 15 |
| 20 | 28 (`== A + D`) | 28 | 0 | 22 |
| 20 | 40 (`2 * fontSize`) | 40 | 6 | 28 |
| 0 | 40 | 28 | 0 | 22 |

The picture is `test-bundles/text-metrics.js` and `goldens/text-metrics.png`: one row per case, each drawing the
`<View>` frame Yoga was given under the `<Text>` fragment background SkParagraph fills, which is the line box. A
row with `letterSpacing` sits beside the same string without it at the same width, because react-native#46436 is
letter spacing changing the line count; and one row is Latin Extended, Greek and Cyrillic — U+01FA, U+01F0,
U+1E9E, U+038F, U+03BE, U+0402, U+045E — at `lineHeight == fontSize`, which is the tall-accent, deep-descender
case that clips first.

Two things this does not prove. The unit gate is a Skia-free build, so the table is arithmetic asserted against
itself and the golden is what ties it to SkParagraph's own metrics; and the Devanagari, Arabic and Tibetan case of
react-native#33704 needs those Noto faces vendored, which is a follow-up on #110 rather than something to
approximate with a font that would resolve through fontconfig. Nothing reports a baseline to Fabric yet either —
there is no `measureLines` and no `onTextLayout`, per *Fidelity limits* — so `measureLineBox` is the arithmetic
that path will report when the vendored header grows it.

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
- **`<TextInput>` is a section of its own.** It has no `platform/cxx` upstream, so its descriptor, shadow node
  and props are ours; the caret, the selection, the composing run and the editing model are in *TextInput*. It
  lays its text out through this same `layoutParagraph`, which is what makes a caret land on the glyph it looks
  like it lands on.

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
  → RetainedScene::writeNode               read off the state and the props into SceneNode::image,
                                           with the pixels the cache already has for that source
  → SceneSnapshot                          SceneImageContent on the primitive, opacity folded in
  → ScenePainter::paintImage               the node's own pixels, fitted and clipped to the frame

  decode worker thread → ImageCache → decode listener → LinuxMountingManager::damageImageSource
                                                     → the pixels, onto every node drawing that URI
```

The pixels travel with the node, and they are the only thing in the scene that is not a number or a string:
`SceneImageContent::pixels` is a `std::shared_ptr<void>`, so `RetainedScene` still links no Skia and the whole of
the image model stays inside the coverage gate, and the painter casts it back to `SkImage` where Skia is linked.
That is upstream's own type erasure — `ImageResponse` carries its bitmap the same way and for the same reason.

The scene asks `decodedImagePixels` for a source at two moments and never at paint time: when it mounts a node,
which covers a source decoded before this node existed, and in `damageImageSource`, which covers a decode that
finished after the node mounted. A frame therefore draws the pixels its nodes were holding when it was
snapshotted, which is what makes the two guarantees of issue #108 hold: **an eviction cannot blank a mounted
node** — react-native-macos [#921](https://github.com/microsoft/react-native-macos/issues/921) is what that looks
like when it can — and **the decoded bitmap is owned by the nodes drawing it and by the cache, and by nothing
else**, so the last node to drop a source, once the cache has evicted it too, is what frees its bytes.

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

A headless run has no run loop to notice that damage, so `BundleRunner` settles the decode queue with
`waitForPendingImageDecodes` **before every read of the scene**, not before the paint: the completion is what
hands the nodes their pixels, so a scene read first would carry none. Settling also makes an image golden
deterministic — without it the same bundle would produce a picture that depends on how fast a codec ran. The
decode worker calls its completions and the listener before it signals idle, for the same reason.

### The cache

`ImageCache` is a least-recently-used cache keyed by source URI, bounded at **64 MiB of decoded pixels**. The
bound is bytes rather than entries because the cost of a decoded image is its pixels and two sources can differ by
three orders of magnitude in area. An insert past the capacity evicts from the least recently used end until the
total fits, and an entry that alone exceeds the capacity is not cached at all, so one oversized image cannot flush
everything else out on its way to being evicted itself. Both a hit and an insert make an entry most recently used.

The value is a `std::shared_ptr<void>`, which is what keeps `ImageContent.cpp` free of Skia and therefore inside
the coverage gate; it is the same type erasure upstream's own `ImageResponse` uses, for the same reason. An
evicted image stays alive as long as a node is still drawing it, because every such node holds the same pointer —
see the pipeline diagram above. `ImageTest.cpp` states the whole of that lifetime, including a fifty-screen
mount-and-unmount loop that ends with as many bitmaps alive as the cache has entries, and no more.

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
  → UIManagerAnimationBackend::trigger  an Animated.event value bound to contentOffset, applied in this frame
                                        — see *Event-driven Animated (#131)*
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

`ScrollController::route` hit-tests with `UIManager::findNodeAtPoint` on the committed shadow tree and then walks
up: `ShadowNodeFamily::getAncestors` returns the
path from the root down as (parent, child index) pairs, and the first `ScrollViewShadowNode` found walking that
path backwards is the innermost ScrollView containing the hit node. That is nested-ScrollView behaviour with no
second rule for it, and a wheel over a node with no ScrollView above it does nothing.

`FabricHost::dispatchInput` hands the whole frame to both routers, and each ignores what the other owns: the
scroll controller routes scroll events only, and the input dispatcher hit-tests everything except a scroll, which
it gives to `PointerRouter::cancelPressForScroll` and to nothing else (#244, *A scroll cancels the press it
started under*). Nothing is hit-tested twice, because a scroll never reaches the hit test. The two still answer
different questions about the same coordinate, and since #97 they read different trees: a click reads the retained
scene, so it lands where the frame painted, and a wheel reads the shadow tree, because the ScrollView a wheel
moves is not what an animation moves. Aligning them is a follow-up in *Hit-testing under animation*.

### Programmatic scrolling (#109)

`scrollTo(x, y, animated)` and `scrollToEnd(animated)` arrive as `dispatchCommand`, which fills a queue on the
JavaScript thread under the mounting mutex. **`FabricHost::advanceScroll` drains that queue at the top of every
frame**, before the physics runs, so a command committed since the last frame is applied by this one rather than
by the one after it. Nothing drained it before this issue: the queue was filled and never read.

What a command turns into is `scrollToDestination` in `ScrollPhysics.h`, which is arithmetic and therefore inside
the coverage gate:

- **A `scrollTo` to the offset the content is already at is not a scroll.** It produces no work, so nothing moves,
  so the cadence emits nothing — no `onScroll` and no momentum bracket. That is the rule
  [core#34327](https://github.com/facebook/react-native/issues/34327) landed after a library built on Paper's
  behaviour looped forever on iOS Fabric, and it needs no special case here: the cadence emits on movement.
- **The destination is clamped before it is returned**, not after the event is sent, which is the other half —
  [core#41034](https://github.com/facebook/react-native/issues/41034) is a fast scroll to the top reporting
  negative offsets to JavaScript. A target that clamps onto the current offset is therefore also no work.
- **An animated scroll reuses the deceleration curve** rather than introducing a second motion model: the velocity
  whose curve travels exactly the remaining distance is what `velocityForTravel` already answers for a wheel
  notch, so an animated `scrollTo` is a flick aimed at a known destination and its bracket is the ordinary
  momentum bracket.
- **The destination is pending until the next frame consumes it.** `advanceTarget` reads the offset it is going to
  compare against before it advances, so an offset written straight into the state would already *be* the previous
  offset and would emit no `onScroll` for a scroll that did move.

`packages/core/test-bundles/scroll-to.js` is the trace, and each command is issued from the previous one's
`onScroll` because the event beat is the only frame boundary a bundle can see:

```text
scroll-to: asking for 120     scroll-to: topScroll at 0,120
scroll-to: asking for -400    scroll-to: topScroll at 0,0
scroll-to: asking for 0       (nothing)
```

The no-op is deliberately last: nothing can be scheduled from its event because there is not going to be one, so
**the run ending in silence is the assertion**. `kHeadlessFrameCount` is five rather than three for it — a command
issued from a frame's event is applied by the frame after it — and every extra frame is empty for a static
fixture, so no golden moved.

### Pressing what a scroll moved (#98)

The cause behind [core#51763](https://github.com/facebook/react-native/issues/51763) and
[core#51290](https://github.com/facebook/react-native/issues/51290) — sticky headers and nested scrollers that
render correctly and ignore every press — is a hit path reading a frame the scroll has translated away from.
Here the offset is applied in `visitNode`, which the painter and the hit test both walk, so there is no second
frame to read; `RetainedSceneScrollTest` in `SceneTest.cpp` pins four consequences of that:

- **A press lands on a row where it is painted, not where it was laid out.** At an offset of 120 the row laid
  out 80 points down paints in the viewport's top band, and its unscrolled position is nothing but ScrollView.
- **The press follows the offset the scene holds this commit.** The controller writes a new offset back every
  frame it moves, and the same point answers differently before and after that write.
- **A horizontal scroller inside a vertical one composes both offsets**, and the innermost cell is what a press
  in it finds.
- **A child painted outside its parent's bounds is pressable there** unless the parent says `overflow: hidden`,
  in which case the clip removes it from the hit region exactly as it removes it from the paint —
  [core#34542](https://github.com/facebook/react-native/issues/34542) is the parent's bounds used as a hit gate
  although the child painted past them.

Sticky headers are the case not covered, because there is no sticky header to press: `stickyHeaderIndices` is a
*ScrollView* deferral, and a test of a header that cannot pin would prove nothing.

### The cadence contract (#45)

`VirtualizedList` windowing is written against a cadence rather than against a picture, so a renderer that gets
the picture right and the cadence wrong makes every list in the ecosystem look broken —
react-native-macos#1119 is twenty-four comments of that. `ScrollEventCadence` is that contract as arithmetic,
inside the coverage gate, and `ScrollController` does nothing with the five events but emit whichever ones it
names.

The five, in the order a frame emits them: `onScrollBeginDrag`, `onScroll`, `onScrollEndDrag`,
`onMomentumScrollBegin`, `onMomentumScrollEnd`. That order is `UIScrollView`'s on the frame that matters — the
release, where the drag's last movement is reported, then the drag ends, then the deceleration begins.

- **The throttle is a minimum interval, not a sampling rate.** `scrollEventThrottle` is milliseconds between
  `onScroll` dispatches; a frame that moved inside the interval is *folded into* the next dispatch rather than
  dropped, so the offset a list sees is always the newest one rather than a stale sample. Zero — which is what an
  unset prop resolves to — means every frame that moved, which is what `RCTScrollView` does with the same value.
- **A boundary always flushes.** The end of a drag and the end of momentum report the movement they are the end
  of whatever the throttle says, so the offset a scroll came to rest at is never the one nobody was told about.
- **Begin and end are paired, exactly once each**, and momentum brackets itself inside the drag bracket.

One consequence to know about: the animated graph is pushed on frames that dispatched an `onScroll` (see *Sync
props fast path*), and the driver listens to that event, so **a throttle throttles scroll-linked native-driver
animation with it**. An app that wants a header to track the scroll should leave `scrollEventThrottle` at 0,
which is the default and the fastest cadence this platform has.

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
- **`onScrollEndDrag`'s `velocity` and `targetContentOffset` are zero.** The pair itself is emitted — see *The
  cadence contract* — but the two fields a paging implementation reads are the landing-point calculation the
  rubber band and snapping also need, so they land with those.
- **Scroll indicators.** `showsVerticalScrollIndicator`, `scrollIndicatorInsets`, `indicatorStyle` and
  `persistentScrollbar` draw nothing. A scrollbar is a painted overlay with its own fade timer and its own hit
  region, which is a component, not a prop.
- **`contentInset`, `contentInsetAdjustmentBehavior`, `scrollAwayPaddingTop`, `centerContent`.** The viewport is
  the ScrollView's frame and the content is `contentBoundingRect.size`; no inset is applied and the offset range is
  `[0, content - viewport]` on each axis. `ScrollEvent::contentInset` is emitted as zero.
- **`maintainVisibleContentPosition`.** Content growing above the viewport currently moves what is on screen. The
  prop is parsed and ignored; honouring it means comparing child frames across commits, which is a commit hook.
- **`scrollResponderScrollTo` and the rest of the responder API.** `scrollTo` and `scrollToEnd` execute — see
  *Programmatic scrolling* — but the legacy responder methods are not bound.
- **Whether `FlatList` windowing behaves correctly at this cadence is still untested**, because there is no
  `FlatList` in this host yet: the fixtures talk to `nativeFabricUIManager` directly, and a real `VirtualizedList`
  needs the React Native JavaScript runtime that arrives with the flagship bring-up (#24). The cadence itself is
  asserted — see below — and the bounded mounted-row count is not.
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

### Hit testing reads the painted scene

`RetainedScene::findNodeAtPoint` walks the retained tree from the surface root and returns the deepest node
painted under the point, together with the absolute origin it was painted at. `UIManager::findNodeAtPoint` — the
same call `NativeDOM` uses for `elementFromPoint` — is the fallback for a painted tag the committed shadow tree
does not contain, and nothing else. The scene answers with a tag; the shadow tree is searched for the node that
tag names, because the event emitter lives there and only there. When nothing is hit the target is the surface
root, which is what lets the hover chain notice that the pointer left a view for the background.

Until #97 this was the other way round, and the reason it changed is *Hit-testing under animation* above: an
animation frame writes the retained scene and commits nothing, so the shadow tree's `LayoutMetrics` describe where
a moving node started rather than where it is. That section holds the decision, the algorithm and the
`measure()` policy.

Reading the shadow tree from the frame thread is allowed rather than tolerated: `ShadowTreeRegistry::visit` and
`ShadowTree::getCurrentRevision` both take a shared lock and are documented as callable from any thread, committed
shadow nodes are immutable, and `EventQueue::enqueueEvent` takes its own mutex. The scene is read under
`LinuxMountingManager`'s `sceneMutex_`, which is the mutex an animation frame's synchronous update already writes
under.

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

### A scroll cancels the press it started under (#244)

The rule, one sentence: **a scroll event that moved the content while a button is held gives up the press target,
so the release that follows is a `pointerUp` with no `click`.** `Pressability` therefore reports `onPressIn` and
`onPressOut` and no `onPress`, and the next press starts clean.

React's responder system already does this on touch — the scroll responder takes over from the touchable and the
press terminates — and it has no wheel path at all, because on a phone there is no wheel. On a desktop the
sequence is button down on a row, wheel or touchpad scroll, button up, and without a platform-synthesized cancel
every list on Linux is [core#48488](https://github.com/facebook/react-native/issues/48488) (a row's `onPress`
firing after a scroll) and [rnw#4614](https://github.com/microsoft/react-native-windows/issues/4614) (the row left
in its pressed style).

`PointerRouter::cancelPressForScroll` is the whole decision, and it is two lines: a scroll whose `scrollAmount` is
zero — `wl_pointer.axis_stop`, or deltas that coalesced to nothing — is not a gesture and is ignored; anything
else clears `pressedTag_`, which is the same field `pointerLeave` clears and the same field the release compares
against before it synthesizes a `click`. `FabricHost::dispatchInput` now hands the whole frame to both the scroll
controller and the input dispatcher rather than splitting it, and `InputDispatcher::dispatch` routes a scroll
event to that one call and to nothing else: no hit test, no focus transition, no caret.

Three consequences worth naming, because each is a choice:

- **The cancel is not a dispatch of its own.** The `onPressOut` arrives with the release, not with the wheel, so
  nothing here invents a `pointerCancel` React would have to be taught to route. An immediate cancel dispatch
  would have to reach the *pressed* node rather than the hit-tested one, which is dispatcher work outside the
  coverage gate for a difference only a still-held button can observe.
- **Any scroll cancels, not only a scroll of the pressed node's own `ScrollView`.** A wheel does not move the
  pointer, so the node under it is the node that was pressed; a subtree test would be state nobody can reach.
- **A press that begins after the scroll settles is untouched**, because the press is what sets the target and
  the scroll before it had nothing to clear.

`InputTest.cpp` table-tests the rule over a wheel notch in both directions, a touchpad delta, a horizontal notch,
an `axis_stop` and a coalesced-to-zero delta, asserting the release is always a `pointerUp` and is a `click` only
when the scroll moved nothing. `packages/core/e2e/press-cancelled-by-scroll.json` is the same sentence under a
compositor: the bundle's every trace line carries a running click count, so the press *after* the cancelled one
still reading `clicks=0` is the assertion that a click did not happen — something an ordered substring match
cannot say any other way. Both layers assert the pointer sequence rather than `onPressIn`/`onPressOut`/`onPress`,
because the bundles in this repository are hand-written against `nativeFabricUIManager` with no React in them and
therefore no `Pressability`; #24 and #22 boot React in the host, and #215 runs upstream's Pressability contracts
against this pipeline, which is where those three names get asserted by their own names.

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
- **IME.** `zwp_text_input_v3` is issue #26 and is implemented; see *IME* below. Pre-edit rendering is no longer
  deferred either — the field of *TextInput* draws it. What is still missing is xkbcommon compose sequences and
  dead keys, which are a keyboard concern rather than an input-method one.
- **E2E traces.** Issue #18 also asks for hover/press traces and keyboard focus order under a headless compositor
  with virtual Wayland input. `--inject-pointer` is the unit-level and integration-level proof; the compositor-level
  one is the first slice of #7 and is `pnpm e2e` — see *E2E driver (#7)*. Keyboard focus order under a compositor
  is still only covered by the one Tab press `text-input.json` makes.

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
implements it with the four methods it already had — `enable`, `disable`, `setSurroundingText` and
`setCursorRectangle` — and `InputDispatcher` calls the first two when focus enters or leaves a component named
`TextInput`, which `src/TextInputComponent.h` registers. The other two follow the caret and are the field's to
supply; see *TextInput*.

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
  commands through `IMountingManager::dispatchCommand`, which queues them in order but executes none of them, and
  they need a JavaScript surface to be called from. Not on the M1 path.
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
- **Keys during composition** is no longer a deferral. While a `zwp_text_input_v3` composition is active no key
  is dispatched at all — not to the focused field and not to React — because text arrives as a commit and only as
  a commit. The rule, and why it is the one place this platform filters a key, is in *TextInput*.
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
`TextInputController` implements it, and `InputDispatcher` routes composition events straight to it — they are the
one input that is not hit-tested, because the target is whatever holds the text cursor rather than whatever is
under the pointer. A composition that arrives with no field focused is dropped rather than queued.

The controller is also what gives `TextInputClient::setSurroundingText` and
`setCursorRectangle(x, y, width, height)` real values: the text around the caret and the caret's rectangle in
surface-local coordinates, so the candidate window lands beside the caret instead of on top of it. Both are sent
for the focused field on every frame its caret moves. See *TextInput*.

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

- **Pre-edit rendering** is no longer deferred: the field of *TextInput* draws the composing run as an underlined
  span and places the caret from the cursor pair `ImePreedit` carries. What is still absent is a *styled* pre-edit
  — version 2's `preedit_hint` would let an input method mark part of a run differently, and there is one style
  here for the whole of it.
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

## TextInput

Issue #17, milestone M1, with the parity matrix of #53 and the key and focus contract of #54 as its acceptance.
This is the component ADR-0001 says is not "working" without IME, and it is six pieces: a component descriptor
that upstream does not ship, a pure editing model, a controller that reconciles that model with React, caret and
selection geometry from the same paragraph the text is drawn with, the painting of all of it, and the key routing
that decides who gets a keystroke.

`react-native-macos` has 79 `TextInput` issues and the only component-specific label in the repository. That list
is this section's specification: macOS inherits `NSTextView` and only has to reconcile with it, and we inherit
nothing — the buffer, the selection model, the caret geometry, the scroll offset, the paste normalisation and the
secure masking are all ours to get wrong.

### The component, and why the descriptor is ours

`ReactCommon/react/renderer/components/textinput` ships the shared half of the component — `BaseTextInputProps`,
`BaseTextInputShadowNode`, `TextInputState` and `TextInputEventEmitter` — and **no `platform/cxx` directory**.
Its own `CMakeLists.txt` globs `*.cpp platform/android/**/*.cpp` unconditionally and puts `platform/android/` on
the public include path; unlike `rrc_view` and `react_renderer_textlayoutmanager` it calls no
`react_native_android_selector`, so there is no platform slot to swap a source into and nothing to swap into it.
The source-list edit that replaced the `TextLayoutManager` and `ImageManager` stubs therefore does not apply.

The other half of the same pattern does. `packages/core/CMakeLists.txt` adds **our own target**, `rnl_textinput`,
over the two vendored translation units we want — `BaseTextInputProps.cpp` and `TextInputEventEmitter.cpp` — and
`src/TextInputComponent.h` defines `TextInputProps`, `TextInputShadowNode` and `TextInputComponentDescriptor` on
top of the base classes. No vendored file is edited, the Android props, shadow node and `MapBuffer` state are
never compiled, and the configure fails loudly with a named error if those two sources ever move.

`TextInputProps` adds exactly three props to the shared base, because upstream keeps them on its platform props:
`secureTextEntry` (iOS carries it inside `TextInputTraits`, Android on `AndroidTextInputProps`), `caretHidden` and
`selectTextOnFocus`. The descriptor constructs the `TextLayoutManager`, exactly as iOS' does, so a field and a
`<Paragraph>` measure through one implementation of SkParagraph.

### The pipeline, end to end

```text
wl_keyboard / zwp_text_input_v3 ─▶ InputQueue ─┐                                    frame thread
                                               │
                            InputDispatcher ◀──┘
                                  │ focused node, key routing, composition
                                  ▼
                          TextInputController ──▶ EditorModel          the buffer, pure, gated at 100%
                                  │                    │
                                  │                    └─▶ TextSegments   ICU graphemes and words
                                  ├──▶ ConcreteState<TextInputState>::updateState   → Yoga → the scene
                                  ├──▶ TextInputEventEmitter    onChange, onSelectionChange, onKeyPress, onSubmitEditing
                                  ├──▶ LinuxMountingManager::setEditorState        caret, selection, composing run
                                  └──▶ TextInputFocusSink       enable, surrounding text, cursor rectangle
                                                       │
                          RetainedScene ◀──────────────┘
                                  │ SceneEditorContent on the primitive
                                  ▼
                          ScenePainter::paintEditor    selection, text, underline, caret
```

The split between `EditorModel` and `TextInputController` is the one this codebase makes everywhere.
`EditorModel.cpp` is pure — no React types, no Skia, no Wayland — and is inside the 100% line-and-branch coverage
gate; `TextInputController.cpp` is a `UIManager` lookup, a state write, four emitter calls and a scene mark, none
of which exists without a committed shadow tree, and `--type` is the test for it.

### The text lives in three places, and that is the contract

React Native's controlled-value contract is not a convention here, it is the reason three copies of the same
string exist and the direction of travel between them:

| Where | What it is | Who writes it |
| --- | --- | --- |
| `EditorModel` | the buffer the user is editing, plus the event count of the last edit | the platform |
| `TextInputState.attributedStringBox` | what is displayed and measured — the **masked** string for a secure field | the platform, after every edit |
| `TextInputState.reactTreeAttributedString` | what React believes the value is, built from `props.text` | upstream's `BaseTextInputShadowNode::updateStateIfNeeded` |

The rule, and both halves of it are load-bearing:

**A value from React is adopted only when `reactTreeAttributedString` changed *and* the `mostRecentEventCount` it
arrived with equals the buffer's.**

- Without the *changed* test, an uncontrolled field is emptied on every re-render: `TextInput.js` sends
  `text: ''` for a field with no `value`, and echoes the event count faithfully, so the counts match and an
  unconditional adopt would delete what the user typed.
- Without the *count* test, a render that is one keystroke behind wins: React re-renders with `value` as it was
  two frames ago, the buffer is replaced, and the caret jumps backwards mid-word. That is
  react-native-macos#2127, and #2066 is the same machinery failing in the other direction.

When an update is ignored the platform writes its own buffer back into the state on the same frame, so the
picture never shows a value React proposed and the platform rejected. `EditorModel::reconcileProps` owns the
count half and is unit-tested against both; `TextInputController::reconcile` owns the changed half.

`mostRecentEventCount` is incremented by an edit and by nothing else. A pre-edit is not an edit — the value React
owns has not changed until the commit arrives — so composing does not move it.

### The editing model

`EditorModel` is byte offsets and nothing else, and every offset it produces is on a grapheme boundary by
construction: the public mutators clamp what they are given, so a caret that would split a UTF-8 sequence or a
combining mark cannot be produced from outside.

| Operation | Rule |
| --- | --- |
| Insert | replaces the selection, or the composing run when one is open — which is why a commit needs no method of its own. |
| Backspace / Delete | remove the selection when there is one, otherwise one **grapheme cluster**, and are a no-op at the ends. react-native-macos#480 is a crash on the second of those. |
| Left / Right | one grapheme; with a selection and no Shift they collapse it to the edge they point at. |
| Ctrl+Left / Ctrl+Right | the start of the previous word and the end of the next, skipping the whitespace run — a boundary list alone would stop inside it. |
| Home / End | the line's own ends, which is the whole buffer for a single-line field. |
| Shift + any of them | moves the caret and leaves the anchor, so the selection grows from where it started. |
| `maxLength` | counted in **UTF-16 code units**, as iOS and Android count it, so an emoji costs two. An insertion is truncated to what fits; one that truncates to nothing is refused rather than deleting the selection. |
| Newlines in a single-line field | collapsed to spaces on the way in, so a multi-line paste cannot put a break in a line that has no way to show it — react-native-macos#2303. |

Grapheme and word boundaries come from ICU, through the same `SkUnicode` instance the paragraph builder uses:
`segmentText` in `TextPipeline.cpp` calls `computeCodeUnitFlags` for grapheme starts and `getUtf8Words` for word
boundaries. `EditorModel` takes them as an injected `TextSegmenter` rather than calling Skia itself, which is what
keeps it in a coverage gate whose binary links no Skia at all. Without one it falls back to
`segmentUtf8CodePoints`, a deliberate subset: every grapheme boundary is also a code-point boundary, so a caret
driven by the fallback never lands inside a UTF-8 sequence — it only fails to treat a combining sequence as one
step. That is the same Skia-off degradation the text pipeline already has rather than a second Unicode
implementation to keep in sync.

### secureTextEntry, and where the masking happens

The buffer is never masked; the string that reaches a paragraph always is. `EditorModel::displayText` replaces
each grapheme cluster with one U+2022, `displayOffsetForByte` and `byteForDisplayOffset` move offsets between the
two, and the controller writes the **masked** string into `TextInputState`. So the paragraph the scene builds,
the paragraph Yoga measures and the string the caret geometry is computed against are all bullets, and the value
exists only in the editor and in the `onChange` payload React needs it in.

That is structural rather than conditional, and it is why every mounted field gets a buffer rather than only the
focused one: `InputDispatcher` collects `TextInput` nodes in the same per-commit walk that collects focusables and
hands them to the controller, so a field that is never focused still publishes its masked string on the first
frame. The headless render paths dispatch one empty input frame before reading the scene for the same reason — a
frame is not only what the compositor sent, it is also where fields publish. react-native-macos#423 is the bug
this arrangement is shaped around.

### Caret and selection geometry

Every rectangle comes from `measureEditorGeometry` in `TextPipeline.cpp`, beside `layoutParagraph` and through it,
so the caret is measured against the paragraph that is drawn. A second layout of the same string would eventually
disagree with the first, and a caret in the wrong place is the most visible bug a text field has —
react-native-macos#1395, #1921 and #2127 are three of them.

- The **caret** is a one-point-wide rectangle taking its height from the glyph after it, so it follows the line
  rather than a constant; at the end of the text the glyph before it supplies the line and the caret sits on its
  trailing edge. Only an empty field invents a height, and only then.
- The **selection** is `getRectsForRange` with `RectHeightStyle::kMax`, so the rectangles of one line share a
  height and a selection across a line break is two boxes rather than a ragged one. That is case 5 of the parity
  matrix and `text-input-multiline-selection.png` is the picture of it: two Tabs reach the wrapped field and
  Ctrl+A selects all 106 characters, so the first line is highlighted to its full width and the second stops
  where the text does.
- The **composing run** is the same call, drawn as a one-point underline along the bottom of each box.
- Offsets crossing into Skia are **UTF-16 indices** into the displayed string, because that is the index space
  SkParagraph's public API speaks. `utf16LengthOfUtf8` and `utf8OffsetForUtf16Index` are the two conversions, and
  they live in `EditorModel.h` because they are arithmetic and arithmetic belongs where the gate can see it.

The declarations are in `src/TextGeometry.h`, which links no Skia, and the definitions are in `TextPipeline.cpp`,
which does. Call sites are guarded by `RNL_ENABLE_TEXT_GEOMETRY`, defined only when the Skia build is on; without
it a paragraph already measures as zero, so a field has no geometry to ask about and the caret still moves by
grapheme because that half is arithmetic.

**The caret height is asserted, not assumed** (#53, case 4). Every `--type` run proves, for each field in the
scene, that the caret is as tall as the line its own midpoint lands in — the same proof the text goldens run for
the paragraph box, extended to the field. `text-input.js` carries four fields that exist only for it: 12, 24 and
40 point, and a 40 point `lineHeight` on a 16 point font. Their measurements are the assertion:

```text
tag 13 caret 16.34 on a 16 point line     tag 15 caret 54.48 on a 54 point line
tag 14 caret 32.69 on a 33 point line     tag 16 caret 40    on a 40 point line
```

The tolerance is one point, because the caret is the line's exact height and the line metric is a rounded one. A
caret that took a constant height — react-native-macos#1395 — would be wrong by tens of points at the fourth
field and by a fraction at the first, so one font size can never carry the others.

**Scrolling is the caret dragging the window**, on whichever axis the field is a window on.
`followedScrollOffset` in `EditorModel.h` is that rule and is the same function for both: the offset moves only
far enough to bring the caret back inside the content box and is clamped to `[0, content - box]`, so a caret in
the middle of the text leaves the window exactly where the last edit left it, and content that fits scrolls to
zero. A single-line field is a window on one long line and scrolls horizontally; a multiline field wraps instead,
so it never scrolls horizontally and scrolls **vertically** when its lines outgrow the box —
react-native-macos#2905 is that feature missing. The painter clips to the content box and translates by both
offsets, and the click-to-caret hit test adds both back, so a press lands on the glyph it looks like it lands on
in a scrolled field.

`text-input-multiline-scroll.png` is the picture: a value four lines long in a box two lines tall, with the caret
at its end, so the last two lines are what is drawn. The rule itself is `FollowedScrollOffsetTest` in
`EditorTest.cpp`, inside the 100% gate.

### The caret blink

Frame-driven, not timer-driven: `FabricHost::advanceCaretBlink` is called once per frame from the window loop,
flips the phase every 600 ms, and every flip goes through `RetainedScene::setEditorState`, which damages that one
field's frame and nothing else. There is no timer in the frame path and ADR-0001 says there will not be one.

A headless run **never advances it**, so the caret in a golden is always in its visible phase and a checked-in
picture is reproducible. Any edit or caret move also resets the phase to visible, which is what makes a caret
stay solid while a key is held down.

### Key routing, and every decision this makes

`InputDispatcher` asks the focused field before it applies the traversal and activation rules, and the field
answers with one of three things: the key was ignored, it was consumed, or it was consumed and focus should
leave. The numbered decisions issue #54 asks to be written down:

1. **Tab always traverses**, in a multiline field as well as a single-line one. A field that inserted a tab would
   be a field with no keyboard way out, and there is no `blurOnSubmit` equivalent for Tab to opt out with.
2. **Enter always fires `onSubmitEditing`** and is never swallowed — react-native-macos#1082. What it does
   besides that is `submitBehavior`, through upstream's own `getNonDefaultSubmitBehavior`: `newline` (the
   multiline default) inserts a line break, `blurAndSubmit` (the single-line default) submits and blurs, and
   `submit` — which is what `blurOnSubmit={false}` compiles to — submits and keeps focus. `onEndEditing` follows
   `onSubmitEditing` in all three of the submitting cases.
3. **Escape blurs.** It is the other keyboard way out, and a field that swallowed both Escape and Enter would be
   a trap.
4. **Ctrl+C, Ctrl+X, Ctrl+V and Ctrl+A reach the field and never the application** — react-native-macos#2075 —
   and `disableKeyboardShortcuts` turns all four off. Ctrl+Left and Ctrl+Right are word motion rather than
   shortcuts. **Ctrl+Z is deliberately not consumed**: there is no undo stack, and swallowing a key that does
   nothing would make undo look implemented and broken rather than absent.
5. **Space types a space**, because it is a character key and the field consumes it before the activation rule
   sees it. On a `<Pressable>` the same key still activates, for the same reason: nothing consumed it first.
6. **A click outside blurs the field and emits `onBlur` once**, which is the focus model's existing rule and
   react-native-macos#999. A click inside places the caret at the glyph nearest the pointer, and a drag from
   there extends the selection.
7. **While a composition is active no key is dispatched at all** — not to the editor, not to React. Text arrives
   as a commit and only as a commit, so a field that also inserted the key events an input method leaves behind
   would double every composed character. That is the react-native-macos#683 and #2312 ordering rule, and it is
   the one place this platform filters a key.

A read-only or non-`editable` field still selects and copies; only the mutating branches are gated.

### The events

Through upstream's `TextInputEventEmitter`, so the payloads are the ones `TextInput.js` already parses:

| Event | When | Payload |
| --- | --- | --- |
| `change` | the buffer differs from what was last reported | the **unmasked** text, the selection, the event count |
| `selectionChange` | the selection differs from what was last reported | the same shape |
| `keyPress` | before the edit a key makes | the key, as upstream's own mapping of the text — an empty string is `Backspace`, a line feed is `Enter` |
| `submitEditing`, `endEditing` | Enter, per the table above | text and event count |

`onChangeText` has no C++ event of its own on any platform: `TextInput.js` derives it from `change`, which is why
`change` is emitted before `selectionChange` — a selection that arrived first would describe a string React has
not been told about yet.

`focus` and `blur` are the focus model's, dispatched by `InputDispatcher` through `ViewEventEmitter` as they are
for every other focusable node. They carry no text or selection, which is a divergence from iOS and is listed as
a deferral below rather than fixed by emitting a second event with the same name.

One reconciliation, one state write and one set of change events happen **per frame**, after the frame's input,
not per event. That is the same batching the event beat already imposes on everything else.

### Content size, and auto-grow (#114)

A field's content size is the size its displayed text measures to at the width the field laid out with —
`measureEditorGeometry`'s `contentWidth` and `contentHeight`, the same measurement the caret comes from — and
`publish` records it on the field every frame. It is what `onContentSizeChange` reports, and the event fires
when and only when that size differs from the last one reported, the first frame a field exists included, which
is the iOS rule and the one [core#52854](https://github.com/facebook/react-native/issues/52854) says Fabric lost.
Without text geometry the size is the container, which is what the metrics carried before.

The order for one keystroke is fixed and is the whole of the contract
[core#46813](https://github.com/facebook/react-native/issues/46813) asks for: the buffer takes the key, the
paragraph is re-measured, the state is written so Yoga relays out, the caret is placed against the new
measurement, and then `onChange`, `onContentSizeChange` and `onSelectionChange` are emitted in that order —
`onContentSizeChange` between the two because it is a consequence of the text and a selection is measured
against the size it reports. `text-input-grow.js` prints that trace, and the e2e scenario of the same name asserts
it under a compositor.

**Why an uncontrolled field never grew.** `BaseTextInputShadowNode::attributedStringBoxToMeasure` consults the
state's string only when the state is "meaningful": a revision past the initial one *and* a font-size multiplier
on its `reactTreeAttributedString` equal to the layout's. `updateStateIfNeeded` is what writes that multiplier,
on the first layout — but it skips a field whose React-tree text is empty, because
`AttributedString::appendFragment` drops an empty fragment and the fresh string is then content-equal to the
empty one the state starts with. The multiplier stays NaN, NaN compares unequal to everything, and every layout
after that measures the placeholder no matter what the state says. That is
[core#54570](https://github.com/facebook/react-native/issues/54570), and it lives in the shared header, not in
a platform. The fix here is `TextInputShadowNode::initialStateData`: the initial state carries the effective text
attributes with the multiplier the root's default `LayoutContext` measures with, so the first platform-side
edit — which advances the revision — is what the next layout measures. `TextInputControllerTest` asserts the
initial state; `text-input-grow-first.png` and `text-input-grow-after.png` are the field one line tall with its
placeholder and three lines tall after `one{Enter}two{Enter}three`, with the sibling below it standing wherever
the field's bottom edge is.

Not done here, and named: a controlled field whose `value` is rejected reverting within one frame with no size
event (item 4 of #114), the first-line position of a fixed-height field across re-renders (item 5), and the
height agreement between a `<TextInput>` and a `<Text>` holding the same string (item 6).

### The proof

```bash
hello_react --type packages/core/test-bundles/text-input.js /tmp/rnl-typing.png "Hello{Left}{Left}X"
```

`packages/core/test-bundles/text-input.js` is three fields in tab order: a plain single-line one with a border, a
radius and a placeholder, a `secureTextEntry` one that already has a value, and a multiline one whose value wraps.
One Tab focuses the first, so everything after it is typed there. The trace begins:

```text
text-input: committed surface 1
text-input: topFocus on plain
text-input: topKeyUp on plain key=Tab
text-input: topKeyDown on plain key=H
text-input: topKeyPress on plain key=H eventCount=0
text-input: topChange on plain text="H" selection=1..1 eventCount=1
text-input: topSelectionChange on plain text="H" selection=1..1 eventCount=1
```

Four things in there are the claims. The first Tab produces **no** `topKeyDown`, because nothing was focused yet
and an unfocused key is dropped — the unhandled-key policy, unchanged. `topKeyPress` carries the event count
*before* the edit and `topChange` the count after it, which is the reconciliation counter moving. The two arrow
presses in the middle of the sequence produce a `topSelectionChange` and no `topChange`, because moving a caret
is not an edit. And `X` lands between the two `l`s, so the run ends with `text="HelXlo"`.

The notation is `parseKeySequence` in `InputPipeline.cpp`, which is inside the coverage gate: every character is
itself, `{Left}` `{Right}` `{Home}` `{End}` `{Backspace}` `{Delete}` `{Enter}` `{Escape}` `{Tab}` name a key,
`{Shift+Left}` and `{Ctrl+A}` add modifiers, and `{Preedit:nih}` and `{Commit:你好}` inject the composition
events an input method would have sent — which is what lets a headless run exercise the IME path with no
compositor and no input method installed. Every event is delivered on its own frame.

Two goldens are registered in `goldens/golden.spec.ts`, both from that one fixture:

- `text-input-typing.png`, from `"Hello{Left}{Left}X"`. The first field reads `HelXlo` with the caret between
  `X` and `l`; the second shows seven bullets and no trace of its value; the third shows its value wrapped onto
  several lines. The first field's border and radius are drawn, which is react-native-macos#2738, and its
  18-point text is drawn in the field's own style, which is #447.
- `text-input-selection.png`, from `"Hello world{Ctrl+A}"`. The same picture with `Hello world` in the first
  field, every character of it behind a translucent selection rectangle, and the caret at the end of it.

### Tests

`EditorModel.cpp` and `Clipboard.cpp` are in `scopedSourcePaths` at 100% line and branch, covered by
`packages/core/tests/EditorTest.cpp`: insertion and deletion at the caret, grapheme-aware motion with and without
a segmenter, word motion including the whitespace-only case, selection extension and collapse, replace-selection,
`maxLength` in UTF-16 code units, single-line newline collapsing, secure masking and both offset conversions,
pre-edit apply/replace/end, commit, `delete_surrounding_text` including the boundary snap, and every
`mostRecentEventCount` reconciliation rule. The crash cases from react-native-macos#480, #486, #432 and #2270 are
regression tests named after what they do rather than after their issue numbers.

`InputPipeline.cpp` gains the key-sequence parser and `isTextKey`, both inside the same gate.
`TextInputController.cpp` and `TextInputComponent.cpp` are outside it for the reason `ScrollController.cpp` is:
what is left in them needs a `UIManager` and a committed tree, and `--type` is the test.

### Deferrals, with owners

- **`wl_data_device`.** Ctrl+C, Ctrl+X and Ctrl+V move text through an **in-process clipboard**
  (`src/Clipboard.cpp`), not the system one. A Wayland clipboard is a data source, a data offer, a MIME
  negotiation and a file-descriptor transfer, all of which need a compositor and a second client to mean
  anything — and the rig for this component is `hello_react`, which has neither. The in-process version is
  behaviourally identical for everything the editing model can get wrong, and it is issue #60 to replace its two
  functions. The primary selection and middle-click paste land with it.
- **Undo and redo.** No stack, and Ctrl+Z is deliberately left unconsumed so the absence is visible.
- **Vertical caret motion.** Up and Down do not move by line: that needs line geometry rather than the grapheme
  and word boundaries the editing model has. A multiline field scrolls to keep the caret visible now, so what is
  missing is the motion, not the window.
- **Bidi caret placement.** The paragraph direction is hardcoded left-to-right, as *Text* already records, so a
  caret in a right-to-left run is placed by visual order and not by logical order. It lands with
  `writingDirection`.
- **Autocorrect, spellcheck and text prediction.** No dictionary service, no underline model, no suggestion UI.
  `autoCorrect` and `spellCheck` are accepted and ignored.
- **Selection handles and the context menu.** Both are touch and mouse affordances with their own hit testing;
  neither is on the M1 path and the Prime Directive says the second consumer has to exist first.
- **Text shadows on a field.** The same deferral *Text* already records for `<Text>`.
- **`onFocus` and `onBlur` payloads.** They arrive through `ViewEventEmitter` with no text or selection in them,
  because emitting a second event of the same name from `TextInputEventEmitter` would double them.
- **`selectTextOnFocus`, `autoFocus` and programmatic `focus()`/`blur()`/`clear()`.** The first two are parsed
  and not acted on, and the third arrives as a Fabric command through `IMountingManager::dispatchCommand`, which
  queues it but executes nothing — the same deferral *Focus and keyboard* records.
- **`set_content_type` and `set_text_change_cause`.** `keyboardType`, `autoCapitalize`, `secureTextEntry` and
  `autoComplete` map onto the content hints `zwp_text_input_v3` carries, and the protocol asks a client to say
  when its text changed for a reason other than composition. Both are small and both need an input method to
  test against, so they wait for the fcitx5 checklist to grow a field to type into.
- **Two paragraph layouts per field per frame.** The painter measures the geometry and then paints, and both
  build the paragraph. SkParagraph's shaped-run cache absorbs the repeat; removing it means a paragraph type
  crossing the Skia-free header boundary, and it belongs with the frame-time work in #20.
- **Auto-sized secure fields.** Yoga measures the masked string, so a field sized by its content is sized by its
  bullets rather than by its value. That is the correct trade — the alternative is the value reaching a
  paragraph — and it is only visible for a field with no explicit width.

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

The seventh and eighth are `packages/core/test-bundles/text-input.js` to
`packages/core/goldens/text-input-typing.png` and `packages/core/goldens/text-input-selection.png`, rendered by
`--type` for the same reason again: a caret, a selection and a composing underline only exist after something has
been typed. One fixture, two sequences. Both pictures show the same three fields — a bordered single-line one, a
`secureTextEntry` one showing seven bullets and no trace of its value, and a multiline one whose value wraps —
and differ in the first field: `HelXlo` with the caret between `X` and `l` in one, `Hello world` entirely behind a
selection rectangle in the other. See *TextInput*.

The ninth is `packages/core/test-bundles/gradient.js` to `packages/core/goldens/gradient.png`, the fixture for
*Gradients* above. Nothing in it needs a vendored asset or font. On the same `#14161A` background:

1. (40, 40) 220x160, 28 px radius, `linear-gradient(45deg, red #E06C75, blue #3366CC)`. The ramp runs toward the
   **top right**, because CSS 0° is up and angles turn clockwise; the two corners it does not point at are the
   pure endpoint colours, and the rounded corners cut the ramp rather than the ramp stopping short of them.
2. (300, 40) 220x160, 28 px radius, a `to bottom right` **corner keyword** with three explicitly positioned stops:
   red at 0%, green `#98C379` at 35%, blue at 100%. The green band therefore sits nearer the top-left corner than
   the middle, which is what makes the explicit percentages visible rather than decorative.
3. (40, 240) 220x160, 24 px radius, a **circle** `farthest-corner` radial from white `#FFFFFF` to slate `#1A1F2B`,
   centred. It must read as a circle in a wider-than-tall box — concentric rings, not an oval — with the corners
   at the far end of the ramp.
4. (300, 240) 220x160, 24 px radius, an **ellipse** `farthest-corner` radial positioned at 30% 30%, amber
   `#E5C07B` to green at 55% to panel `#1E2430`. The rings are elliptical and their centre is up and to the left,
   so the bottom-right corner is the farthest point and the darkest.
5. (560, 40) a 200x360 amber `#E5C07B` panel, 20 px radius, holding a 160x320 child inset 20 px with 16 px radius,
   `opacity: 0.5`, and a `180deg` white-to-slate gradient. The child is the opacity assertion: the gradient is
   half-transparent over amber, so the top reads as pale amber and the bottom as a muted brown, and neither end is
   the flat white or flat slate a renderer that dropped the opacity would draw.

The tenth is `packages/core/test-bundles/rounded-box.js` to `packages/core/goldens/rounded-box.png`, issue #99's
fixture: four cards, each one a place where two of the consumers listed in *One rounded box* meet on the same arc.
If any of them computed its own rounded rect the arcs would separate and this image would change.

1. (60, 60) 320x240 dark `#1E2430`, 96 px radius, 16 px amber `#E5C07B` ring, `overflow: hidden`, holding a
   400x300 blue `#61AFEF` child offset (40, 120). The child stops on the parent's bottom arc and draws **over**
   the ring where it reaches it, which is what `clipsToBounds` does on iOS.
2. (440, 60) 320x240, 96 px radius, a `to bottom right` gradient and a 16 px blue ring. The gradient must stop on
   exactly the arc a solid fill would have.
3. (60, 360) 320x180 cyan `#56B6C2` asking for a 200 px radius with a 24 px purple `#C678DD` ring. The clamp makes
   it a pill; the ring's inner edge is the same pill inset by 24 on both axes, which is the CSS inner-radius rule
   at its maximum.
4. (440, 360) 320x180 dark, the same clamped pill, `overflow: hidden`, holding a red `#E06C75` child the size of
   the whole box. Every pixel of the child's square corners is cut, so the picture is the parent's pill in the
   child's colour inside the parent's amber ring.

The eleventh is `packages/core/test-bundles/border-matrix.js` to `packages/core/goldens/border-matrix.png`, issue
#100's, in five rows:

1. **Uniform** — square and rounded rings, one side's width alone on a rounded box
   ([core#37954](https://github.com/facebook/react-native/issues/37954)), a translucent ring over an opaque fill
   ([core#39286](https://github.com/facebook/react-native/issues/39286)), and a translucent ring on a rounded box
   ([core#49606](https://github.com/facebook/react-native/issues/49606)).
2. **Per side, three scales** — the same four-colour tile at 1x, 1.5x and 2x. This is the seam row: a seam is a
   scale-dependent artifact, so one scale would not find it.
3. **Transparent and unset** — a transparent top beside three opaque sides, square and rounded, which must leave
   the other three intact ([core#34722](https://github.com/facebook/react-native/issues/34722)); then the two
   tiles that draw no border at all, an unset colour and an explicitly transparent one, which are the same value
   in upstream's type and therefore the same picture; then a zero width beside a set one.
4. **Hairlines** — 0.25, 0.5, 0.75 and 1 point uniform, and 0.4 on a rounded box. At this rig's point scale every
   one of them is promoted to one device pixel and every one of them is visible
   ([core#58054](https://github.com/facebook/react-native/issues/58054)).
5. **Large radius** — a clamped pill with a ring, per-side widths and per-side colours at a 40 px radius, and a
   circle, so the mitres meet on an arc rather than on a straight edge.

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

### The measured-paragraph proof (#41)

`--text-fit-golden` is the same idea applied to text, and it is the answer to issue #41 that this architecture
admits.

The deep half of that issue cannot fail here by construction: measurement and painting call **one**
`layoutParagraph`, so they cannot shape a string differently, and the scene carries the `AttributedString` and
`ParagraphAttributes` from `ParagraphState` to the painter untouched. What they can differ in is the **width they
are called with**. `TextLayoutManager::measure` is given Yoga's constraint and answers with
`ceil(getLongestLine())` by `ceil(getHeight())`; Yoga then assigns a frame from that answer; and the painter lays
the same string out again at the content box inside that frame. A paragraph that re-wraps at the second width
overflows the box that was measured for it, which is react-native-macos#2857 — the phantom content size that a
window resize "fixes" by forcing a re-measure.

So for every text node in the scene the run lays the paragraph out at the width the painter uses and fails when
it is taller or wider than the box, with one point of slack for Yoga's rounding:

```text
[golden] tag 3 was given a 300x20 box and paints 3 lines of 294.96x66
```

It then measures the same string through `TextLayoutManager` twice. The second call is a cache hit, so the pair
is the cache cross-check the issue asks for — a measure cache that could answer differently from the paragraph
behind it is the other half of the same bug — and the answer is compared against the paragraph that was painted.

`measureParagraphMetrics` in `TextGeometry.h` is what makes the paint side observable: line count, per-line width
and height, the longest line and the total. It is declared beside the caret and hit-test geometry for the same
reason they are there — the definitions live with `layoutParagraph`, so nothing measures a second paragraph to
ask a question about the first.

**Three more properties of the measure path** ride on the same run (#111), because a fixture is where a real
`AttributedString` with real fonts and real fragments comes from, and a table of strings is not:

- **A paragraph shrinks to its longest line.** Measured against a constraint far wider than the text, the answer
  is the width the glyphs occupy rather than the constraint —
  [core#54571](https://github.com/facebook/react-native/issues/54571) is multi-line `<Text>` that stopped being
  able to.
- **An empty paragraph has no width.** The same attributed string with its fragments removed measures zero, which
  is [core#55468](https://github.com/facebook/react-native/issues/55468) the right way round.
- **The cache key includes the paragraph attributes.** A paragraph that wraps onto more than one line must measure
  strictly shorter when it is limited to one; a cache keyed on less than it depends on answers with the height it
  already had. Verified as a check that can fail: removing the limit makes it report `tag 11 measured 98 points
  tall over 4 lines and 98 limited to one`.

**The precondition, and it is a real one.** A box smaller than its own text is a legitimate thing for an author to
write — an explicit `height` on a `<Text>` overflows on every platform — and this proof cannot tell that apart
from a paragraph that re-wrapped. So it runs on the fixtures whose text boxes are auto-sized, which today is
`text.js` and `text-metrics.js`, and a fixture that deliberately constrains a paragraph belongs on plain
`--golden`.

`--text-fit-golden` replaces `--golden` for those two rather than adding a golden of its own: the picture is the
same picture, and it is now written only if the paragraph in it fits the box it was measured into.

### The first-frame proof (#46)

react-native-macos [#2857](https://github.com/microsoft/react-native-macos/issues/2857) is a large `<Text>` in a
`ScrollView` producing "massive vertical and horizontal overflow on the first render", repaired by resizing the
window. The shape is always a content size derived from a measurement that was not yet final and never
recomputed, and it is the shape this stack is *more* exposed to, not less: the first frame runs before the
fractional scale is known and before an image has decoded.

Two things make it not happen here, and both are stated rather than hoped for:

- **The content size is the committed layout.** Upstream's `ScrollViewShadowNode::layout` recomputes
  `contentBoundingRect` from its children's committed frames on *every* layout and writes it into
  `ScrollViewState` in the same commit, and `ScrollController::advance` reads it back off the newest clone every
  frame. There is no cached content size anywhere on this side to go stale.
- **A resting offset is re-clamped every frame.** `decelerateAxis` with zero velocity clamps the offset it already
  has against the content it now has, so content that shrinks below the offset pulls the offset down with it in the
  same frame and the movement emits an `onScroll`. `AnOffsetAtRestFollowsContentThatShrankBelowIt` and its two
  neighbours in `ScrollTest.cpp` are that rule, inside the gate.

`--first-frame-golden` is the bug as one assertion. `runFabricBundleAcrossFrames` snapshots the scene at the
first commit **with image decodes still in flight** — `waitForFirstCommit` is told not to settle them, unlike the
damage proof, which wants the first frame with its pixels attached — and again once everything that settles late
has settled: timers drained, decodes finished, the headless frames run. Every primitive's **frame, matrix and clip
frames** have to be equal between the two. Settling before the first snapshot would make the "first" scene the
settled one and the comparison test nothing. Pixels are deliberately not compared: an image that decoded between the snapshots is supposed to appear, and
the layout under it is what must not move. The negative control is `damage.js`, whose second commit unmounts a
view, and it fails with `the first frame painted 3 primitives and the settled one 2`.

`scroll-first-frame.js` is the fixture: one ScrollView holding a paragraph that wraps onto many lines, a column
of images whose decodes finish after the mount, and a nested ScrollView, over a marker directly below the
viewport that any first-frame overflow would have landed on.

Not covered: the window-rig capture of frame 1 and frame 60 needs the compositor and belongs with the e2e work,
and `maintainVisibleContentPosition` stays where the *ScrollView* deferrals already record it — parsed and
ignored, because honouring it means comparing child frames across commits, which is a commit hook.

### The hit-versus-paint agreement proof (#35)

`--hit-paint-golden` is the second fixture of that kind: issue #35's acceptance criterion — "the node reported as
the pointer target at a point is the node whose pixels are visible at that point" — turned into an assertion, with
the PNG as a by-product.

The trick that makes it a proof rather than a restatement is the fixture. Every node in
`packages/core/test-bundles/hit-paint.js` paints a colour no other node paints, so **a pixel names the node that
painted it**; the run samples the *live* scene's `findNodeAtPoint` over a grid of the same surface, which is the
same answer the input dispatcher would get; and the two are compared. Neither side is derived from the other: one
is Skia's rasteriser, the other is `RetainedScene`'s tree walk.

Three rules keep the comparison honest:

- **Samples are taken at pixel centres.** A pixel's colour describes the area around its centre, so `(x + 0.5,
  y + 0.5)` is the point whose hit answer that colour can be compared against. At an axis-aligned edge the
  distinction does not show; on a rotated edge it is the whole disagreement, because the corner of a fully painted
  pixel can sit outside the shape that painted it.
- **A blended pixel is skipped.** An anti-aliased edge is a mixture of two nodes' colours, and "the node visible
  here" has no answer there.
- **Too few comparable samples fails the run.** Without that floor — three quarters of the grid — a fixture that
  blended everything would pass by skipping everything.

A mismatch names the point, the colour and both tags: `at (312, 208) the picture is #61AFEF, which is tag 7, and
the press lands on tag 5`.

The fixture is seven cases, each one a way the picture and the click come apart in the `react-native-macos`
reports that motivated the issue: two overlapping siblings; the same pair with `zIndex` reversing them
(rn-macos#1842); a child translated out of its slot (rn-macos#2147); the same square rotated about `center`, `top
left` and `bottom right` (rn-macos#2361); a scaled square; a child under a rounded `overflow: hidden` parent; and
nested transformed ancestors, which have to compose rather than last-wins. `packages/core/e2e/hit-paint.json`
drives four of them through a compositor's `wl_pointer` and asserts the target by name.

**What it found.** Two disagreements, both real and both fixed rather than tolerated:

1. **Containment is half-open on the right and the bottom.** A box at x = 30 that is 150 wide paints the columns
   30 to 179 and leaves 180 to whatever is behind it, but `Rect::containsPoint` — which upstream's hit test uses —
   closes both intervals, so a press at 180 landed on a node that had painted nothing there.
   `roundedBoxContainsPoint` is half-open on those two edges now.
2. **The sampling correspondence above**, which is a property of the proof rather than of the renderer, and is
   documented here so the next person does not rediscover it as a false positive.

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

Only weston is supported *by this rig*. `cage` and `sway` were considered and rejected for it rather than
deferred: cage launches a client instead of offering a socket, which is a different shape. The e2e driver of #7
does run under cage, because weston can inject no input at all — see *E2E driver (#7)* for the evidence — and the
two rigs stay on the compositor each one's job needs.

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

## E2E driver (#7)

Issue #7, milestone M1. A real bundle under a real compositor, driven by virtual Wayland input, asserted on the
event trace it prints, on the `wp_presentation` frame times it reports and on the picture it captured. The first
slice was the compositor rig, the injector and the trace; the second added the frame-timing probe, the perf gate
and the screenshot comparison — see *Frame timing* and *Screenshots*. What is still missing is named at the end.

### The compositor decision, and the evidence for it

The e2e rig runs under **cage**, not under the weston the window goldens use. weston cannot inject input at all on
a machine that installed it from a distribution package:

- weston's only injection surface is its own `weston-test` protocol (`protocol/weston-test.xml`), implemented by
  the `weston-test.so` plugin in `tests/`, which upstream builds with `install: false`. Ubuntu 24.04's `weston`
  package ships ten plugins under `/usr/lib/x86_64-linux-gnu/weston/` and none of them is that one.
- weston implements neither `zwlr_virtual_pointer_manager_v1` nor `zwp_virtual_keyboard_manager_v1`, and neither
  protocol is part of wayland-protocols, so nothing under `/usr/share/wayland-protocols` describes them either.

cage is a wlroots kiosk compositor: one window, always focused, no configuration. Ubuntu 24.04 ships
`cage 0.1.5+20240127`, whose `cage.c` creates both `wlr_virtual_keyboard_manager_v1` and
`wlr_virtual_pointer_manager_v1` and attaches every device they create to its seat, and its wlroots backend runs
headless through `WLR_BACKENDS=headless` with no DRM device and no seat of its own. sway would work too and was
not chosen: it needs a configuration file and a window manager's worth of behaviour between the injector and the
surface, where cage fullscreens exactly one client so injected output coordinates are the surface's coordinates.

Two compositors in one repository is a cost, and it is paid deliberately: the window golden rig needs a socket to
connect a client to, which weston offers and cage does not, and this rig needs input injection, which cage offers
and weston does not. Neither rig can do the other's job today.

The virtual devices only exist while `rnl_inject` is connected. Attaching them is also what makes cage advertise
`wl_seat` pointer and keyboard capabilities, so the window under test binds `wl_pointer` and `wl_keyboard` in the
middle of its run — `WaylandSeat::updateCapabilities` already handles that, and wlroots sends a fresh keymap and,
for the focused client, a `wl_keyboard.enter` when the resource is created.

### The injector

`packages/core/src/InputInjector.cpp` builds `rnl_inject`, a Wayland client with no window: it binds
`zwlr_virtual_pointer_manager_v1`, `zwp_virtual_keyboard_manager_v1`, `wl_seat` and `wl_output`, creates one
virtual pointer and one virtual keyboard on the seat, uploads a pinned `us` keymap through a `memfd`, and then
executes a script from a file argument or from stdin. Neither protocol XML is in wayland-protocols and Ubuntu has
no `wlr-protocols` package, so both files are vendored under `packages/core/protocols/` from wlroots 0.17.4 — the
wlroots cage links against — and `wayland-scanner` generates `rnl_virtual_input` from them the way `rnl_xdg_shell`
is generated from `xdg-shell.xml`.

The script is one command per line, `#` comments ignored:

```text
move <x> <y>                 # absolute, in output pixels
click <x> <y>                # move, then a left press and release
button left|middle|right press|release
wheel up|down <notches>      # one vertical axis_discrete carrying the signed notch count
key <keysym> press|release   # an xkb keysym name, for example Tab or Return
type <text>                  # ASCII, shifted characters send the shift modifier
sleep <milliseconds>
```

`wl_output`'s current mode is what the absolute motion's extents are, so a coordinate in the script is a pixel on
the output; cage gives its single client the whole output, so it is also a pixel in the surface. Keysyms are
resolved against the uploaded keymap by walking it — `xkb_keymap_key_get_syms_by_level` for every keycode and
level — rather than against a table of characters, which is what makes `type` and `key` the same lookup, and the
evdev keycode both protocols carry is the keymap's keycode minus the X11 offset of 8 that *Input* describes.

### The rig

`scripts/e2e.ts` is the driver. Per scenario it creates a private `XDG_RUNTIME_DIR`, starts

```text
cage -- rnl_window --fabric <bundle> --frames <n> --screenshot build/e2e/<scenario>/screenshot.png \
     --frame-log build/e2e/<scenario>/frames.jsonl
```

with `WLR_BACKENDS=headless`, `WLR_RENDERER=pixman`, `WLR_LIBINPUT_NO_DEVICES=1`, `XKB_DEFAULT_LAYOUT=us` and the
lavapipe ICD pinned by the same discovery `scripts/window-golden.ts` exports, waits for the wayland socket to
appear in that directory, waits for the scenario's `ready` line on the window's stdout, runs `rnl_inject` with the
scenario's steps on stdin, and then waits for the window to exit on its own frame budget.

The event trace is the bundle's own `console.log` output: `rnl_window` has no trace format, and every fixture
bundle already prints one line per Fabric event through `registerEventHandler`. cage passes its child's stdout
through, so the trace is what the driver collects from the compositor process. Expectations are ordered
substrings — each has to appear on a line after the previous one — which is what makes them assertions about a
sequence of events rather than a set.

Every run writes `build/e2e/<scenario>/trace.log`, `screenshot.png` and `frames.jsonl`, and CI uploads that tree
**always**, not only on failure: a passing run's frame log is the baseline the next run is read against, and it is
where the picture a screenshot golden gets blessed from comes out.

### The scenarios

`packages/core/e2e/*.json`, one object per file:

```json
{
  "name": "pressable-click",
  "bundle": "pressable.js",
  "ready": "pressable: committed surface 1",
  "frames": 600,
  "steps": ["sleep 500", "move 200 140", "sleep 100", "click 200 140", "sleep 500"],
  "screenshot": { "golden": "pressable-click.png", "maxDifferentPixels": 50 },
  "expect": ["pressable: topClick on box at 200,140"]
}
```

`frameBudget` and `screenshot` are both optional; a scenario without either asserts only on its trace.

`pressable.json` clicks the middle of the 200x120 view `pressable.js` places at (100, 80) and expects the
`topPointerDown`, `topPointerUp` and `topClick` lines the `--inject-pointer` proof in *Input* produces headlessly,
now produced by a compositor's `wl_pointer` instead. `text-input.json` presses Tab to focus the first field of
`text-input.js`, types `Hi`, and expects `topFocus on plain` and the two `topChange` lines — which is also the
proof that the shift modifier reaches the field, since `H` is `shift+h` on the injected keymap.

`text-input-editing.json` is the editing half of the parity matrix (#53): Tab into the field, type, select all
with Ctrl+A, copy, move to the end and paste, asserting `text="Hi"`, `selection=0..2` and `text="HiHi"` in that
order. It is the scenario that needed `rnl_inject` to be able to **hold** a modifier — `key Control_L press`
updates a mask that every key after it carries, and releasing it clears the mask — because until then the
injector only ever sent the shift a shifted character implies, and no chord was expressible at all.

`rounded-press.json` is the e2e half of issue #99. `rounded-press.js` places a 200x120 card at (100, 80) with a
60-point radius over a full-surface container, and the scenario clicks twice: (105, 85), five points inside the
frame on both axes but 77.8 from the corner's arc centre at (160, 140), and (200, 140) on the card itself. The
first click has to be reported on the container and the second on the card, so the trace is the hit result
flipping at the arc rather than at the bounding rectangle.

`scripts/e2e/scenario.ts` holds everything pure — scenario validation, the ordered-substring matcher and artifact
naming — and `scripts/e2e/scenario.spec.ts` covers it at the repository's 100% threshold. `frame-log.ts` and
`screenshot.ts` are pure in the same sense, and `grade.ts` is the file reading around them: it is the only part of
the driver that touches the filesystem and is still covered, because a temporary directory is all its tests need.
The process side stays in `scripts/e2e.ts`, uncovered by Vitest for the reason `scripts/doctor.ts` is: it is
spawns and sockets.

### Frame timing

ADR-0001 decision 3 names three mechanisms and this is the one that **measures**. `wl_surface.frame` throttles and
its timestamp has an undefined base; `wp_fifo_v1` and `wp_commit_timer_v1` schedule; `wp_presentation_feedback` is
purely retrospective and reports when a content update actually turned into light. A frame time is therefore the
delta between two consecutive `presented` timestamps, and nothing in this rig reads a wall clock.

`presentation-time.xml` is stable and ships in wayland-protocols, so unlike the two virtual-input protocols it
needs no vendoring: `packages/core/CMakeLists.txt` runs `wayland-scanner` over
`${RNL_WAYLAND_PROTOCOLS_DIR}/stable/presentation-time/presentation-time.xml` into `rnl_presentation_time`, exactly
as it does for `xdg-shell` and `text-input-unstable-v3`, and its absence joins `RNL_WINDOW_MISSING` rather than
failing the configure. `WaylandWindow` binds `wp_presentation` at `min(advertised, 2)` — decision 3 wants version
2 because Hyprland zeroes the refresh hint for version 1 clients under VRR, and a compositor stuck at 1 still
binds. One protocol quirk is load-bearing and is the only thing a comment marks in `WaylandWindow`: the generated
header declares a *function* named `wp_presentation_feedback`, which hides the struct of the same name in C++, so
the type has to be spelled `struct wp_presentation_feedback` wherever it is named.

`requestPresentationFeedback` sits beside `requestFrameCallback` inside `SkiaVulkanRenderer::drawFrame`, before
the present request, because `vkQueuePresentKHR` is what commits the surface and the feedback request has to
attach to the same pending content update. Both `presented` and `discarded` are destructor events, so each handler
destroys its own proxy. `presented` records `{seq, presented ns, refresh ns, flags}`; `discarded` only counts.

The arithmetic is `FrameTiming` (`packages/core/src/FrameTiming.{h,cpp}`), a pure class in the shape of
`FrameClock`: no Wayland, no clock reads, every value passed in, and therefore under the 100% line-and-branch C++
gate through `packages/core/tests/FrameTimingTest.cpp` and `scripts/cpp-coverage.ts`. Percentiles are **nearest
rank** — the sample at `ceil(fraction * count)`, one-based, no interpolation — so every number reported is a frame
time that actually happened. Intervals live in a ring of the last 4096 samples, because a long-running window must
not grow a vector for the life of the process.

`rnl_window --frame-log <path>` writes it as JSON Lines. One record per presented frame:

```json
{"seq":41,"presentedNs":1016000000,"refreshNs":16666666,"flags":1,"frameNs":16000000}
```

`frameNs` is the delta to the previous presented frame and is **absent on the first one**, because there is
nothing to measure it from. The last line is the summary:

```json
{"summary":true,"frames":240,"discarded":0,"p50Ns":16000000,"p95Ns":16600000,"maxNs":31000000}
```

`frames` counts `presented` events, `discarded` counts `discarded` ones, and the three percentiles are over the
intervals, of which there is one fewer than there are frames. A compositor that advertises no `wp_presentation`
global at all is not an error: the window runs unchanged and the log is one summary line with `"frames":0` and
`"unsupported":true`. cage is wlroots, and wlroots creates `wlr_presentation` — but the headless backend's
`presented` timestamps come from its own software frame timer rather than from a vblank, so expect `flags` of 0
and a refresh hint that is the headless output's nominal mode. **This has not been observed running**: no cage and
no lavapipe exist on the machine this was written on, so the fallback path is reasoned about, not exercised. If it
turns out cage advertises nothing, `findFrameBudgetFailures` in `scripts/e2e/frame-log.ts` is the one place that
decides whether that is a failure or a skip, and today it is a failure with the reason named.

The gate is `frameBudget` on a scenario: `p95Ms` is the ceiling and `minFrames` is how many frames must have been
presented for that percentile to mean anything, so a run that presented four frames cannot pass by accident. The
driver reports both reasons at once rather than hiding the second behind the first, and prints the whole summary
as a note on every run, budget or not, so the numbers are tracked rather than only gated.

`animated-frames.json` is the perf scenario: `animated.js` for 240 frames at `{"p95Ms": 16.7, "minFrames": 60}`.
**16.7 ms, not the gospel's 8.33 ms, and deliberately so.** A headless lavapipe rig composited by pixman under
cage is not going to hold a 120 Hz budget, and a gate that fails on the runner rather than on the renderer is a
gate nobody will keep. 16.7 ms is the CI regression gate; the 8.33 ms number in the testing gospel is a **real
hardware** budget and is measured on real hardware, not here.

### Screenshots

The driver now compares as well as captures, which is a deliberate departure from the split *Window goldens* makes
between `scripts/window-golden.ts` and `golden.spec.ts`. Two constraints forced it. `pngjs` lives in
`packages/core` and the root workspace had none, so `pngjs` and `@types/pngjs` are now root devDependencies —
there is no way to decode a PNG from a root script without one. And oxlint's `import/no-relative-parent-imports`
forbids a root script reaching into a package, so `compareImagesPerceptually` is **not** importable from
`scripts/e2e`; `scripts/e2e/screenshot.ts` carries its own comparator instead.

That comparator keeps the window goldens' `MAX_CHANNEL_DELTA` of 8 — a lavapipe frame's antialiasing coverage is a
function of the Mesa version, not of the renderer — and replaces their 1%-of-the-frame budget with the absolute
`maxDifferentPixels` the scenario declares. At cage's output size 1% is thousands of pixels, which would let a
whole widget move; 50 is a handful of antialiased edges and nothing else.

Blessing a golden:

1. Add `"screenshot": {"golden": "<name>.png", "maxDifferentPixels": 50}` to the scenario. A golden that does not
   exist yet is a **note, not a failure** — the driver prints the path it would have compared against and the path
   of the picture to bless — so the run that produces the first artifact stays green.
2. Take `screenshot.png` from that run's `e2e` CI artifact, which is why the upload is `if: always()`.
3. Commit it as `packages/core/e2e/goldens/<name>.png` and review the picture in the PR diff, exactly as the
   window goldens are reviewed.

`pressable-click.png` was wired this way and skipped for exactly as long as nobody blessed it — the expectation
was declared, the file did not exist, and the screenshot half of the driver therefore asserted nothing. It is
committed now, at cage's own output size of **1280x720** rather than the 800x600 the window goldens use, taken
from CI run 33779667104's artifact. What made it blessable rather than a flake waiting to happen is that the same
picture came out of two independent runs **byte for byte**: 0 differing pixels between 33779667104 and
33778339030, against a tolerance of 50.

### Running it

```bash
cmake --build build/dev --target rnl_window
cmake --build build/dev --target rnl_inject
pnpm e2e                        # every scenario
pnpm e2e --scenario pressable-click
pnpm e2e --scenario animated-frames
```

Like the window rig, the driver exits **2** with the reason on stderr when `cage`, the lavapipe ICD or either
binary is missing, so a checkout without a graphics stack stays green. In CI that exit is a failure, which is what
stops an incomplete apt list from passing as a silent skip. The `window` job installs `cage`, builds `rnl_inject`
after the window goldens step, runs `pnpm e2e`, and always uploads `build/e2e` as the `e2e` artifact.

### What is still outstanding

Named here so they are not mistaken for oversights, all of them still #7:

- **A structured event trace.** Today the trace is the fixture's `console.log` output, which means a scenario can
  only assert on what its bundle happens to print. A `--trace` flag on `rnl_window` emitting typed lines is what
  would let a scenario assert on events no fixture was written to report.
- **Deterministic capture.** The window captures at a fixed frame number, so a scenario sizes `frames` to cover
  its own injection. Capturing on demand — a signal, or a step in the script — would remove the budget entirely.
- **Screenshot goldens beyond the first.** `pressable-click.png` is blessed and asserting; the other scenarios
  declare no screenshot, so an unexpected picture in them is still only caught by their event traces.
- **One comparator, not two.** `scripts/e2e/screenshot.ts` and `packages/core/goldens/perceptual-diff.ts` now
  carry the same per-channel tolerance for the same reason, because oxlint forbids the root script importing the
  package one. Moving the shared half into a package both can depend on is the fix, and it is not worth a package
  for one function yet.
- **The 8.33 ms budget.** The CI gate is 16.7 ms because the rig is lavapipe and pixman. Measuring the gospel's
  real number needs real hardware and a place to record the result over time; neither exists yet.

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
`SceneTest.cpp`, `InputTest.cpp`, `FocusTest.cpp`, `ImeTest.cpp`, `ImageTest.cpp`, `ScrollTest.cpp` and
`EditorTest.cpp` plus the nine sources they exercise — `RetainedScene.cpp`, `LinuxMountingManager.cpp`,
`InputPipeline.cpp`, `FocusModel.cpp`, `ImageContent.cpp`, `ScrollPhysics.cpp`, `TextInputV3State.cpp`,
`EditorModel.cpp` and `Clipboard.cpp`, compiled
directly into the test binary rather than linked from a Hermes-linked library, plus `TextInputComponent.cpp` and
the `rnl_textinput` target, which the scene needs to read a field's props — and registers them with
`gtest_discover_tests` so `ctest` finds every `TEST` individually. Under Clang, `rnl_core_tests` also gets
`-fprofile-instr-generate -fcoverage-mapping`, LLVM's source-based coverage instrumentation.

`scripts/cpp-coverage.ts` is the gate: it runs `rnl_core_tests` with `LLVM_PROFILE_FILE` pointed at
`build/test/coverage`, merges the raw profile with `llvm-profdata merge -sparse`, exports it as lcov with
`llvm-cov export --format=lcov`, and grades line and branch coverage per file against an explicit list
(`scopedSourcePaths` in the script — today `RetainedScene.cpp`, `LinuxMountingManager.cpp`, `InputPipeline.cpp`,
`FocusModel.cpp`, `ImageContent.cpp`, `ScrollPhysics.cpp`, `TextInputV3State.cpp`, `EditorModel.cpp` and
`Clipboard.cpp`, the nine sources the test
binary actually exercises). A source with no tests behind it is
deliberately not in that list: adding a file there without coverage behind it is what turns the gate red, rather
than a silent average across the whole of `packages/core`.

`InputDispatcher.cpp`, `WaylandSeat.cpp`, `TextInputClient.cpp` and `TextInputController.cpp` are the input
sources deliberately left outside that scope, and the split between them and `InputPipeline.cpp`,
`FocusModel.cpp`, `TextInputV3State.cpp` and `EditorModel.cpp` is
what makes the scope honest rather than convenient: everything that
can be arithmetically wrong — motion coalescing, the queue bound, the buttons bitmask, the press-to-click state
machine, offset points, modifier flags, the key-name and key-code tables, the key-sequence notation, the
traversal order and its wraps,
the `done` batching, ordering and serial rules of `zwp_text_input_v3`, and the whole of the text buffer, its
selection, its composition range and its `mostRecentEventCount` reconciliation — lives in those four where the
gate sees it. What is left in
`InputDispatcher.cpp` is a scene hit test, a shadow-tree lookup by tag and a switch over five emitter calls — the
geometry it used to own now lives in `RetainedScene::findNodeAtPoint`, which the gate does see — what is
left in `WaylandSeat.cpp` is protocol plumbing that needs a compositor, what is left in `TextInputClient.cpp`
is requests and listeners that need one too, and what is left in `TextInputController.cpp` is a shadow-node
lookup, a state write and four emitter calls. `--inject-pointer`, `--focus-tab`, `--ime-debug` and `--type` are
the tests for those four.

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
