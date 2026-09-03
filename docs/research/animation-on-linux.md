# Animation on Linux: the native driver, the worklets runtime, and Reanimated 4

- Research report, 2026-09-03. Written for the planning issue #95, under epic #1, milestone M4.
- Verification legend: **[V]** verified by reading the primary source on this machine or through `gh api`,
  **[I]** inferred from what was read, **[?]** not confirmed.
- The machine-readable plan this document explains is `scripts/issue-plans/animation.json`: **19 proposed
  issues** in three sequenced phases, each with `phase` and `dependsOn`.

## Sources and versions

| Artifact | Version | Where it was read |
| --- | --- | --- |
| `react-native` (vendored) | **0.87.1** | `third_party/react-native/packages/react-native`, pinned by `scripts/vendor.lock.json` (`tag: v0.87.1`) [V] |
| `react-native` (flagship) | 0.86.0 | `~/suuudokuuu/node_modules/react-native` [V] |
| `react-native-reanimated` | **4.6.0** (npm `latest`, 2026-09) | `~/budgie/node_modules/.pnpm/react-native-reanimated@4.6.0_.../node_modules/react-native-reanimated` [V] |
| `react-native-worklets` | **0.12.1** (npm `latest`) | `~/budgie/node_modules/.pnpm/react-native-worklets@0.12.1_.../node_modules/react-native-worklets` [V] |
| `react-native-reanimated` (flagship pin) | 4.5.0 | `~/suuudokuuu/packages/app/package.json` [V] |
| `react-native-worklets` (flagship pin) | 0.10.0 | `~/suuudokuuu/packages/app/package.json` [V] |
| `react-native-gesture-handler` (flagship pin) | 2.32.0 | `~/suuudokuuu/node_modules/react-native-gesture-handler` [V] |

Reanimated 4.6.0 declares `peerDependencies: { "react-native": "0.83 - 0.87", "react-native-worklets":
"0.12.x" }`, and its `compatibility.json` lists `0.83`–`0.87` for the `4.6.x` line [V]. **Our vendored React
Native is inside Reanimated's supported range today.** That will not stay true — ADR-0001 plans to sit three
to six minors behind — but it is true at the moment this plan is written, and it is the reason to research
this now rather than after the drift starts.

---

## 0. Headline answers

1. **React Native 0.87 ships a complete C++ implementation of the `Animated` native driver, and it is already
   wired for C++ host platforms.** `ReactCommon/react/renderer/animated/` is a full `NativeAnimatedNodesManager`
   (41.9 KB of `.cpp`), every animated node type, three animation drivers, the event driver, and `AnimatedModule`
   — a `NativeAnimatedModuleCxxSpec` TurboModule. `ReactCxxPlatform`'s `ReactCxxTurboModuleProvider` hands it out
   by name. The native driver is **not** per-platform ObjC/Java any more, and we do not have to write one. [V]
2. **The one thing a new platform must implement is `AnimationChoreographer` — three virtual methods.** Upstream's
   own documentation says so in as many words: *"For any new platform that wants to adopt the Animation Backend,
   this is the part that needs to be implemented."* It is a direct fit for `FrameClock` (#59). [V]
3. **`react-native-worklets` is close to portable.** Its `Common/cpp` tree has ~15 conditional-compilation points,
   every one of them in logging, tracing, or the opt-in fetch preview, and every one degrades to a no-op on a
   third platform. The platform contract is essentially one class with one pure-virtual method (`UIScheduler::
   queryIsOnUIThread`), one `std::function` for `requestAnimationFrame`, and a five-method `PlatformLogger`. [V]
4. **`react-native-reanimated` is not portable, and the blocker is four lines long.** `PlatformDepMethodsHolder.h`
   defines `SynchronouslyUpdateUIPropsFunction` under `#ifdef ANDROID` / `#elif __APPLE__` with **no `#else`**,
   and then declares a struct member of that type. On Linux the struct does not compile. There are **99**
   `ANDROID`/`__APPLE__` conditionals in Reanimated's `Common/cpp`, against 15 in worklets'. [V]
5. **Reanimated 4.6 already targets React Native's shared animation backend.** `ReanimatedModuleProxy` guards on
   `#if REACT_NATIVE_VERSION_MINOR >= 85` and, when the `USE_ANIMATION_BACKEND` static feature flag is on, runs its
   *entire* per-frame loop through `UIManagerAnimationBackend::start(...)`. **Phase 1 and Phase 3 share a frame
   source.** Whatever we build for `Animated` is the thing Reanimated will ride on. [V]
6. **No out-of-tree platform has ever ported Reanimated natively, and the newest evidence sharpens that.**
   `react-native-windows` runs Reanimated through the **JavaScript fallback** — the web implementation — not a
   native port. `react-native-macos` is not a port either: it reuses the `apple/` directory. Software Mansion
   merged [software-mansion/react-native-reanimated#10117](https://github.com/software-mansion/react-native-reanimated/pull/10117),
   *"fix: enable JavaScript fallback on Windows"*, into `main` on **2026-09-02 — the day before this report**. [V]
7. That fallback is the **de-risking rung this plan is built around**: a Linux platform can run the flagship's
   Reanimated code on the JS thread long before it can run a worklet, and deciding whether to take that rung is
   the first P0 of Phase 2, not an afterthought.

---

## 1. What React Native 0.87 gives us for native-driven `Animated`

### 1.1 The C++ animated node graph exists, and it is not a stub

`third_party/react-native/packages/react-native/ReactCommon/react/renderer/animated/` [V]:

```text
AnimatedModule.{h,cpp}                     the TurboModule (NativeAnimatedModuleCxxSpec)
NativeAnimatedNodesManager.{h,cpp}         41.9 KB — the graph
NativeAnimatedNodesManagerProvider.{h,cpp} the shared-instance holder a host passes around
MergedValueDispatcher.{h,cpp}
EventEmitterListener.h
nodes/    Value, Props, Style, Transform, Interpolation, Addition, Subtraction, Multiplication,
          Division, Modulus, DiffClamp, Round, Color, Object, Tracking  (16 node types)
drivers/  AnimationDriver, FrameAnimationDriver, SpringAnimationDriver, DecayAnimationDriver
event_drivers/ EventAnimationDriver
internal/ AnimatedMountingOverrideDelegate, NativeAnimatedAllowlist.h, primitives.h
tests/    AnimatedNodeTests, AnimationDriverTests, DecayAnimationDriverTest,
          EventAnimationDriverTests, ManagedPropsMountingOverrideTests
```

`AnimatedModule` derives from `NativeAnimatedModuleCxxSpec<AnimatedModule>` and `TurboModuleWithJSIBindings`, and
implements the full nineteen-operation `NativeAnimatedModule` contract as a `std::variant<Operation>`:
`CreateAnimatedNode`, `GetValue`, `Start`/`StopListeningToAnimatedNodeValue`, `Connect`/`DisconnectAnimatedNodes`,
`StartAnimatingNode`, `StopAnimation`, `SetAnimatedNodeValue`, `SetAnimatedNodeOffset`,
`Flatten`/`ExtractAnimatedNodeOffset`, `ConnectAnimatedNodeToView`, `ConnectAnimatedNodeToShadowNodeFamily`,
`DisconnectAnimatedNodeFromView`, `RestoreDefaultValues`, `DropAnimatedNode`, `AddAnimatedEventToView`,
`RemoveAnimatedEventFromView` [V].

The spec header it consumes is `FBReactNativeSpec/FBReactNativeSpecJSI.h` — the codegen artifact
`react_codegen_rncore` already builds in `packages/core/CMakeLists.txt` [V]. Nothing here needs new codegen.

**`react/renderer/animated` is not in our build graph.** `packages/core/CMakeLists.txt` lists
`react/renderer/animationbackend` and links `react_renderer_animationbackend`, but neither `react/renderer/animated`
nor `react_renderer_animated` appears [V]. Adding it is one line in each of two lists; its `target_link_libraries`
names `react_renderer_{core,graphics,mounting,uimanager,scheduler,animationbackend}` and `react_codegen_rncore`,
all of which are already in the graph [V].

### 1.2 The shared animation backend, and the one hole a platform fills

`ReactCommon/react/renderer/animationbackend/` is twelve files plus `__docs__/AnimationBackend.md` [V]. The
document is worth quoting because it settles the design question for us:

> The backend is not meant to be used directly. Its main purpose is to serve as a layer over the Fabric renderer
> that animation frameworks (like Animated or **Reanimated**) can use.

and:

> ### AnimationChoreographer
> This is an abstraction layer that wraps the native frame callback mechanisms. On iOS, it has a corresponding
> `RCTDisplayLink`, and on Android, it uses the `Choreographer`. **For any new platform that wants to adopt the
> Animation Backend, this is the part that needs to be implemented.**

`AnimationChoreographer.h` in full is 40 lines. The platform contract is:

```cpp
class AnimationChoreographer {
 public:
  virtual void resume() = 0;
  virtual void pause() = 0;
  virtual AnimationTimestamp now() const;               // defaulted to steady_clock
  void setAnimationBackend(std::weak_ptr<UIManagerAnimationBackend>);
  void onAnimationFrame(AnimationTimestamp timestamp) const;   // call this per frame
};
```

Its own comment says it "serves as an interface for native animation frame scheduling that can be used as
abstraction in ReactCxxPlatform" [V]. `AnimationTimestamp` is `std::chrono::duration<double, std::milli>` [V].

The frameworks' side of the seam is `UIManagerAnimationBackend`, seven pure virtuals — `onAnimationFrame`,
`start(Callback) -> CallbackId`, `stop(CallbackId)`, `clearRegistry`, `clearRegistryOnSurfaceStop`, `trigger`,
`pushAnimationMutations`, `registerJSInvoker` — with `Callback = std::function<AnimationMutations(AnimationTimestamp)>`
[V]. `AnimationBackend` splits each frame's mutations: if a batch has no layout updates it goes through
`synchronouslyUpdateProps`, otherwise it performs a Fabric commit, and `AnimationBackendCommitHook` re-applies
animated props on top of every React commit so a mid-animation re-render does not flash a stale frame [V].

### 1.3 How `ReactCxxPlatform` wires it, and why we cannot just copy that

`ReactCxxPlatform/react/runtime/ReactHost.h` takes `std::shared_ptr<NativeAnimatedNodesManagerProvider>` and
`std::shared_ptr<AnimationChoreographer>` as constructor parameters, holds an `AnimatedMountingOverrideDelegate`,
sets `toolbox.animationChoreographer` on the `SchedulerToolbox`, and passes the provider to
`ReactCxxTurboModuleProvider`, which returns an `AnimatedModule` for `AnimatedModule::kModuleName` [V]. Its
`CMakeLists.txt` links `react_renderer_animated` and `react_renderer_animationbackend` [V].

We do not use that `ReactHost`. `packages/core/CMakeLists.txt` records why: *"ReactCxxPlatform's ReactHost is not
[used], because it pulls bridgelesshermes, the HTTP and WebSocket clients, and the whole nativemodule tree"* — we
take only `ReactCxxPlatform/react/renderer/{uimanager,scheduler}` and `react/threading` [V]. So the wiring is ours
to write, but it is **wiring, and we have the reference implementation in-tree**.

### 1.4 The two things standing between us and a native-driven animation

**We register no TurboModules at all.** `ReactHost::ReactHost()` calls
`reactInstance_->initializeRuntime({}, installConsoleBinding)` and nothing else; there is no `TurboModuleBinding`,
no module provider, no `react/nativemodule/*` subdirectory in the CMake list [V]. `AnimatedModule` cannot be
reached from JavaScript until that exists. This is the true first task of Phase 1, and it is shared infrastructure
that #23 (core native modules) and #79 (networking) need anyway.

**Feature flags default off.** `ReactNativeFeatureFlagsDefaults.h` has `cxxNativeAnimatedEnabled() → false`,
`useSharedAnimatedBackend() → false`, `optimizedAnimatedPropUpdates() → false` [V]. Our `ReactHost` installs
`ReactNativeFeatureFlagsOverridesOSSStable`, which overrides only `enableBridgelessArchitecture` and
`useNativeViewConfigsInBridgelessMode` [V]. `ReactNativeFeatureFlagsOverridesOSSCanary` is the upstream class that
turns `cxxNativeAnimatedEnabled` on, and it is generated, so we subclass rather than adopt it [V].

**We leave `synchronouslyUpdateViewOnUIThread` defaulted.** `IMountingManager` declares it
`virtual void synchronouslyUpdateViewOnUIThread(Tag, const folly::dynamic&) {}` and `LinuxMountingManager` overrides
only `executeMount` and `dispatchCommand` [V]. Every animated frame would therefore take the Fabric-commit path
even when nothing about it touches layout — the exact cost the backend's fast path exists to avoid.

### 1.5 What the changelog says about when this landed

From `third_party/react-native/CHANGELOG.md` [V]:

- **0.85.0** — *"`AnimationChoreographer` interface with an implementation for fantom tests"*; *"Enable
  `-DRN_USE_ANIMATION_BACKEND` by default"*; *"Add c++ AnimatedModule to DefaultTurboModules"*; *"Added support for
  transform operations"*; *"Animated calls `AnimationBackend::trigger` to push updates from events to the mounting
  layer"*; *"AnimationBackend docs"*.
- **0.86.0** — bug fixes only, including *"Fix 1-frame latency in C++ `NativeAnimatedNodesManager` for event-driven
  animations by processing the animation graph synchronously on every scroll event, matching the Java implementation
  behavior"* — a useful hint about the semantics our event-driven work must match.

The vendored `CHANGELOG.md` tops out at 0.86.0; the 0.87 entries are not in it [V]. **[?]** whether 0.87 changed
anything in this area beyond what is visible in the source.

**Bottom line for Phase 1:** this is integration work against an in-tree, tested, upstream-documented C++
subsystem, not an implementation of an animation engine. That is the single most consequential fact in this report
and it should re-scope #19 downward.

---

## 2. `react-native-worklets` 0.12.1 — the seams a new platform fills

### 2.1 The C++ tree

`Common/cpp/worklets/` is 74 files in eight directories [V]:

```text
AnimationFrameQueue/   AnimationFrameBatchinator      coalesces rAF callbacks onto one frame
Compat/                Holders, StableApi             version-shim surface
NativeModules/         WorkletsModuleProxy            the host object every platform constructs
                       JSIWorkletsModuleProxy         the `globalThis` binding
Registries/            WorkletRuntimeRegistry
RunLoop/               AsyncQueue, AsyncQueueImpl, EventLoop
SharedItems/           Serializable, Shareable, SerializableFactory, SerializableRemoteFunction,
                       Synchronizable, MemoryManager, UnpackerLoader
Tools/                 UIScheduler, JSScheduler, JSLogger, PlatformLogger, ThreadSafeQueue,
                       FeatureFlags, JSISerializer, VersionUtils, WorkletsVersion, ScriptBuffer
WorkletRuntime/        WorkletRuntime, WorkletHermesRuntime, RuntimeManager, RuntimeBindings,
                       UIRuntimeDecorator, WorkletRuntimeDecorator, RNRuntimeWorkletDecorator,
                       ScriptLoader, BundleModeConfig, HermesProfiling
```

`WorkletHermesRuntime.h` includes `<hermes/hermes.h>` directly [V]. Worklets is a Hermes-first library; ADR-0001
decision 5 makes that an advantage rather than a constraint.

### 2.2 The construction seam

```cpp
WorkletsModuleProxy(
    jsi::Runtime &rnRuntime,
    const std::shared_ptr<CallInvoker> &jsCallInvoker,
    const std::shared_ptr<UIScheduler> &uiScheduler,
    std::function<bool()> &&isJavaScriptQueue,
    const std::shared_ptr<RuntimeBindings> &runtimeBindings,
    const BundleModeConfig &bundleModeConfig,
    const std::shared_ptr<RNRuntimeStatus> &rnRuntimeStatus);
```
[V] Everything the platform supplies is in that list.

### 2.3 The platform contract, item by item

| Seam | What it is | Our answer | Classification |
| --- | --- | --- | --- |
| `UIScheduler` | one pure virtual `queryIsOnUIThread() const`, plus overridable `scheduleOnUI` / `triggerUI`; the base class already owns the `ThreadSafeQueue` and the `scheduledOnUI_` flag | a `LinuxUIScheduler` whose `queryIsOnUIThread` compares against the frame thread's `std::thread::id` and whose `scheduleOnUI` posts to the frame loop | **needs-platform-impl**, small. `IOSUIScheduler.mm` is 25 lines [V] |
| `RuntimeBindings::requestAnimationFrame` | `std::function<void(std::function<void(const double)>)>` | `FrameClock`, the same source Phase 1 uses | **needs-platform-impl**, trivial |
| `RuntimeBindings::nativeLoggingHook` | a `jsi::HostFunctionType` lifted off the RN runtime's `global.nativeLoggingHook`; the header ships `extractNativeLoggingHookFromRNRuntime` | reuse the provided helper; our `ConsoleBinding` already installs the hook | **portable** |
| `PlatformLogger` | five static `log` overloads | ~10 lines over `std::cerr` / our logger | **needs-platform-impl**, trivial |
| `ScriptLoader::loadScript` | header-only static; intercepts Metro's `__r`, evaluates the bundle, restores `__r`, then requires entry-point `-2` | nothing to write | **portable** |
| `WorkletsModule` TurboModule host | `apple/WorkletsModule.mm`, `android/WorkletsModule.cpp` | a C++ TurboModule that constructs the proxy and installs `__workletsModuleProxy`; needs the codegen'd `rnworklets` spec | **needs-platform-impl**, medium |
| tracing (`_beginSection`/`_endSection`) | `#ifdef ANDROID` … `#elif defined(__APPLE__)` … `#endif` with no `#else` **inside a function body** | compiles to a no-op on Linux; a Tracy binding is a later nicety | **portable** |
| `WORKLETS_FETCH_PREVIEW_ENABLED` | opt-in; the only place worklets needs networking | leave off | **portable** |

Conditional compilation across `Common/cpp`: **15 sites**, all in `WorkletRuntimeDecorator.cpp` (logging/tracing),
`WorkletsSystraceSection.h`, `RuntimeBindings.h` (fetch preview), and `WorkletsVersion.cpp` (`#ifdef
WORKLETS_VERSION`). Every one of them is a statement-level or member-level addition with a safe empty branch [V].
**Nothing in worklets' shared C++ is hard-blocked on a third platform.**

### 2.4 Build integration

`android/CMakeLists.txt` is a usable template [V]: glob `Common/cpp/worklets/*.cpp` plus the platform directory,
`-DWORKLETS_VERSION=…`, `-DWORKLETS_FEATURE_FLAGS="…"`, optional `-DWORKLETS_PROFILING`, `-DNDEBUG` outside Debug,
`target_compile_reactnative_options`, and include paths into `ReactCommon`, `callinvoker`, `runtimeexecutor`,
`jsiexecutor`, and — already — `react/renderer/graphics/platform/cxx`. It links `jsi`, `hermes-engine::hermesvm`
and the merged `ReactAndroid::reactnative`; we would link our object libraries instead.

`codegenConfig` is `{ name: "rnworklets", type: "modules", jsSrcsDir: "src/specs", android: {…} }` — no `ios` key,
no platform gate on the module spec itself [V]. Our parallel codegen driver (#21) has to accept it.

The Babel plugin is `react-native-worklets/plugin` and is platform-agnostic [V]. `react-native.config.js` is listed
in the package's `files` array but is **absent from the published 0.12.1 tarball layout on this machine** [V] —
**[?]** whether it ships in other builds. Either way, autolinking for `platform: linux` is our CLI's problem (#21,
#22), not the library's.

### 2.5 The threading question this raises for us

Worklets' UI runtime is a **second Hermes runtime**, owned by whatever thread `UIScheduler` calls the UI thread.
On iOS and Android that is the main/UI thread, which does not also drive the GPU. Ours does: `WindowMain`'s run
loop is the frame thread, and it acquires the swapchain image, paints through Skia, and presents. Putting a JS
runtime on it means a worklet can push a frame past the compositor's deadline. See *Risks*, item 1 — this is the
architectural decision inside Phase 2, and it is not one the library makes for us.

---

## 3. `react-native-reanimated` 4.6.0 — the seams, and the one that does not compile

### 3.1 `PlatformDepMethodsHolder` is the literal platform contract

`Common/cpp/reanimated/Tools/PlatformDepMethodsHolder.h`, read in full [V]. The struct:

```cpp
struct PlatformDepMethodsHolder {
  RequestRenderFunction requestRender;
#ifdef ANDROID
  PreserveMountedTagsFunction filterUnmountedTagsFunction;
#endif
#ifdef __APPLE__
  ForceScreenSnapshotFunction forceScreenSnapshotFunction;
#endif
  SynchronouslyUpdateUIPropsFunction synchronouslyUpdateUIPropsFunction;
  GetAnimationTimestampFunction getAnimationTimestamp;
  RegisterSensorFunction registerSensor;
  UnregisterSensorFunction unregisterSensor;
  SetGestureStateFunction setGestureStateFunction;
  KeyboardEventSubscribeFunction subscribeForKeyboardEvents;
  KeyboardEventUnsubscribeFunction unsubscribeFromKeyboardEvents;
  MaybeFlushUIUpdatesQueueFunction maybeFlushUIUpdatesQueueFunction;
  PlatformAttachPseudoSelectorFunction attachPseudoSelector;
  PlatformDetachPseudoSelectorFunction detachPseudoSelector;
  css::CSSCanRoutePropertyFunction cssCanRouteProperty;
  css::CSSApplyTransitionFunction cssApplyTransition;
  css::CSSRemoveTransitionFunction cssRemoveTransition;
  css::CSSGetPlatformValueFunction cssGetPlatformValue;
  // Last so a platform that does not supply it can omit it and get value-init.
  std::shared_ptr<css::CSSPlatformAnimationFactory> platformAnimationFactory;
};
```

The two `#ifdef`-guarded members are additive and vanish harmlessly. `synchronouslyUpdateUIPropsFunction` does not:

```cpp
#ifdef ANDROID
using SynchronouslyUpdateUIPropsFunction = std::function<void(const std::vector<int> &, const std::vector<double> &)>;
#elif __APPLE__
using SynchronouslyUpdateUIPropsFunction = std::function<void(const int, const folly::dynamic &)>;
#endif // ANDROID
```

There is **no `#else`**. On a platform that is neither, the alias is undefined and a *required* struct member has no
type. `PlatformDepMethodsHolder.h` does not compile on Linux [V]. This is the single sharpest, smallest, most
fixable blocker in the entire program, and it is upstream RFC ask #1.

The trailing comment — *"Last so a platform that does not supply it can omit it and get value-init"* — shows the
maintainers are already thinking about platforms that supply a subset. That is the wedge the RFC argues from.

### 3.2 Where the port is dense

99 `ANDROID`/`__APPLE__` conditionals in `Common/cpp` [V], clustered:

| Area | Sites | Character |
| --- | --- | --- |
| `LayoutAnimations/` (`Proxy_Legacy`, `Proxy_Experimental`, `ProxyCommon`, `PropsDiffer`, `SharedTransitions`) | ~44 | the densest cluster by far; the legacy and experimental proxies are near-parallel implementations, both heavily `#ifdef ANDROID` |
| `NativeModules/ReanimatedModuleProxy.{h,cpp}` | ~14 | synchronous-update policy, the synchronous prop-name allowlist (`shadowOffset`/`shadowOpacity`/`shadowRadius` are Apple-only), Android draw-pass event handling |
| `Fabric/updates/` (`UpdatesRegistry`, `UpdatesRegistryManager`, `PropsLayoutFilter`) | ~12 | Android's int/double buffer protocol for batched JNI prop updates |
| `CSS/common/values/` (`CSSNumber`, `CSSBoxShadow`) and `CSS/utils/platform.cpp` | ~10 | unit handling and the platform routing decision |
| `Tools/` (`SingleInstanceChecker`, `ReanimatedSystraceSection`) | ~6 | diagnostics; safe |

Encouragingly, some of these already have a **third branch**. `CSS/utils/platform.cpp`'s `canRouteCSSProperty` ends
`#else // No native routing backend on this platform yet; every property runs on the loop. return false; #endif`,
and `shouldUseSynchronousUpdatesInPerformOperations` has an `#else { return false; }` [V]. Software Mansion has
started writing the generic branch. They have not finished, and nothing forces them to keep it compiling.

### 3.3 The Apple side is the size of the port

`apple/reanimated/` is **40 files** [V]: `PlatformDepMethodsHolderImpl.{h,mm}`, `NativeProxy.{h,mm}`,
`ReanimatedModule.{h,mm}`, `REANodesManager.{h,mm}`, `READisplayLink.h`, `REAKeyboardEventObserver`,
`sensor/ReanimatedSensor*`, `pseudoSelectors/REAPseudoSelector*` + `REATouchHoverCoordinator`,
`CSS/REACSSPlatformProps` + `REACSSPlatformTransitions`, `REAReducedMotion.h`, `REASlowAnimations`,
`RNGestureHandlerStateManager.h`, `view/REASharedTransitionBoundaryView`, `RCTUIView+Reanimated`. That list is a
fair specification of Phase 3's platform half.

### 3.4 The good news: Reanimated 4.6 already rides React Native's animation backend

`ReanimatedModuleProxy.h` [V]:

```cpp
#if REACT_NATIVE_VERSION_MINOR >= 85
#include <react/renderer/animationbackend/AnimationBackend.h>
#include <react/renderer/uimanager/UIManagerAnimationBackend.h>
#endif
```

and `ReanimatedModuleProxy.cpp` [V]:

```cpp
void ReanimatedModuleProxy::startBackendIfNeeded() {
#if REACT_NATIVE_VERSION_MINOR >= 85
  if constexpr (StaticFeatureFlags::getFlag("USE_ANIMATION_BACKEND")) {
    ...
    animationBackendCallbackId_ = getAnimationBackend()->start([weakThis = weak_from_this()](AnimationTimestamp t) {
      ... return strongThis->runGrandCallback(t, GrandCallbackSource::AnimationLoop);
    });
```

with `getAnimationBackend()` reaching it through `uiManager_->unstable_getAnimationBackend()`, and
`stopBackendIfIdle` calling `stop(callbackId)` once nothing has work [V]. `pushAnimationMutations` is used for the
synchronous/event path on RN ≥ 0.85.2 [V]. `CSSAnimationsRegistry`, `CSSTransitionsRegistry`, `AnimatedPropsRegistry`
and `UpdatesRegistry` are all guarded the same way [V].

Two consequences, both large:

- **The frame source is shared.** A correct `LinuxAnimationChoreographer` from Phase 1 is also Reanimated's frame
  source in Phase 3. `PlatformDepMethodsHolder::requestRender` becomes the legacy path.
- **The prop-application path is shared.** Reanimated's mutations go through `AnimationBackend`'s
  `synchronouslyUpdateProps` / commit split and its commit hook, exactly like `Animated`'s. We implement
  `synchronouslyUpdateViewOnUIThread` once.

`USE_ANIMATION_BACKEND` is a **static** flag: `StaticFeatureFlags::getFlag` parses the `REANIMATED_FEATURE_FLAGS`
compile definition and, when that macro is absent, `getFlag` returns `false` for everything [V]. Our build must set
`-DREANIMATED_FEATURE_FLAGS="[USE_ANIMATION_BACKEND:true]…"`, the way `android/CMakeLists.txt` does [V]. Getting
that string wrong is a silent behaviour change, not a build error.

### 3.5 What the other platforms actually did

- **`react-native-macos`: not a port.** The Reanimated monorepo has `apps/macos-example` alongside
  `fabric-example`, `tvos-example`, `web-example`, `next-example` and `common-app` [V]. macOS-specific issues are
  build-and-example maintenance ([#9700](https://github.com/software-mansion/react-native-reanimated/issues/9700)
  *"Compile CSS Core Animation platform props on macOS"*,
  [#9679](https://github.com/software-mansion/react-native-reanimated/issues/9679),
  [#9536](https://github.com/software-mansion/react-native-reanimated/issues/9536) *"disabled macos builds in CI
  pipeline"*) [V]. macOS reuses `apple/`; it is not evidence that a third platform is cheap.
- **`react-native-windows`: the JavaScript fallback, not a port.** Of 29 title matches for "windows", all but one
  are about Windows *as a build host* — Babel plugin paths, bundle-mode module IDs, a Windows CI runner [V]. The
  exception is [#10117](https://github.com/software-mansion/react-native-reanimated/pull/10117), merged into `main`
  on **2026-09-02**, whose description is the clearest statement of RNW's real status that exists:

  > Reanimated previously routed React Native Windows through the JavaScript implementation introduced in #4917.
  > The fallback regressed after #8371 moved Worklets' native implementation to `.native.ts` files. Metro considers
  > Windows a native platform and therefore selects `.native` before the unsuffixed JavaScript implementation. […]
  > This PR therefore extends `wrapWithReanimatedMetroConfig` so that internal relative imports from Reanimated and
  > Worklets do not prefer `.native` files on Windows.

  It was verified by bundling and running a real application on `react-native-windows` 0.85.3, new architecture [V].

**So there is no native out-of-tree Reanimated port anywhere, and the maintained third-platform story is a Metro
resolver that steers a native platform onto the web implementation.** ADR-0001's M4 sentence — *"a decision no
out-of-tree platform, Microsoft or Huawei, has ever made"* — survives verification, and now has a documented
alternative attached to it.

The fallback is not free, and the plan should not pretend otherwise. It is keyed on `Platform.OS === 'windows'`
(`IS_WINDOWS` in `src/common/constants/platform.ts` [V] — true of 4.5.x; 4.6.0 deleted `SHOULD_BE_USE_WEB` and replaced the runtime branch with a 38-file `.native.*` suffix split, which is why `packages/reanimated` pins 4.6.0 and ships an empty patch queue; see `docs/platform-identity.md`); `linux` inherits nothing. It regressed once and went
unnoticed until an outside contributor fixed it. In 4.6.0 as published, the non-`.native` `NativeWorklets.ts` is
literally `export const WorkletsModule: IWorkletsModule = null!` [V] — the fallback ships broken in the version on
disk, and the fix is unreleased. Taking this rung means depending on a path with one consumer and no CI on our
platform. It is still the right first rung, because it converts "Reanimated or nothing" into "Reanimated slowly,
then Reanimated properly".

### 3.6 What the flagship actually needs

Suuudokuuu is not a light Reanimated consumer [V, `~/suuudokuuu/packages/app/src`]:

- **23 files** import `react-native-reanimated` directly; 2 import `react-native-worklets`.
- API frequency: `useAnimatedStyle` 37, `withTiming` 36, `useSharedValue` 32, `Easing` 30, `interpolate` 28,
  `interpolateColor` 15, `withRepeat` 10, `withSequence` 8, `FadeIn` 7, `withDelay` 6, `withSpring` 5, `runOnJS` 5,
  `useAnimatedReaction` 2.
- **Layout animations are in use**: `entering: FadeIn.delay(...).duration(...)` and `exiting: FadeOut...` in
  `challenge-technique-preview`, `challenge-conditions-row`, `challenge-race-badge`.
- **Gesture integration is in use**: 7 `GestureDetector`, with `Gesture.Tap`, `Gesture.Pan`, `Gesture.Race`.
- Transitively, `expo-router`, `react-native-screens`, `react-native-drawer-layout`, `react-native-unistyles`,
  `react-native-gesture-handler` and `reanimated-color-picker` all declare `react-native-reanimated` [V].

`react-native-unistyles` 3.3.0 lists `react-native-reanimated` **and** `react-native-nitro-modules` as peers [V].
`react-native-gesture-handler` 2.32.0 ships `android/` and `apple/` and no shared C++ at all [V] — it is a separate
port under #87, out of scope here, and Phase 3 owns only the `setGestureStateFunction` seam between them.

**Declining Reanimated is equivalent to declining the flagship.** ADR-0001's M4 "port-or-decline decision" is
therefore not evenly balanced, and this plan treats it as sequenced de-risking rather than an open question.

---

## 4. Risks

Ordered by how much of the program each one can invalidate.

### 4.1 A JavaScript runtime on the thread that paints

Our frame thread is `WindowMain`'s run loop: it acquires a swapchain image, paints through Skia, flushes, submits,
and presents (*Window host*, `docs/cpp-toolchain.md`). Worklets wants a UI runtime on "the UI thread", and
`UIScheduler::scheduleOnUI` executes inline when `isOnUIThread()` — `IOSUIScheduler::scheduleOnUI` does exactly
that [V]. A worklet that runs long therefore lands *inside* our frame, in the middle of the 8.33 ms budget, with no
preemption.

The candidate answers, none free:

1. **UI runtime on the frame thread.** Matches iOS/Android semantics exactly; a slow worklet drops frames. The
   compositor's deadline is the only backpressure.
2. **A dedicated worklet thread with its own `AsyncQueue`.** `RuntimeManager::createWorkletRuntime` already takes an
   `AsyncQueue` and an `enableEventLoop` flag [V], so the machinery exists. Cost: `runOnUI` is no longer
   synchronous-when-already-on-UI, and Reanimated's assumption that a shared value written in a worklet is visible
   to the same frame's prop application needs re-verifying. Divergence from the reference platforms is a permanent
   bug source.
3. **Frame-thread runtime with a per-frame work budget** that defers overflow to the next tick, measured by the
   perf gate.

This is the decision that has to be made *before* Phase 2 code is written, and it should probably be an ADR
amendment rather than an issue comment. ADR-0001 decision 6 already declares the frame thread "our concept and not
a React Native one"; this is where that bites.

Related hazard: `HermesJSRuntimeFactory` builds one runtime for the instance's JS thread. Hermes runtimes are not
thread-safe, and `WorkletHermesRuntime` wraps its own with a `WorkletsReentrancyCheck` decorator copied from RN's
`HermesExecutorFactory` [V] — which is compiled in only when `NDEBUG` is undefined. Our TSan job is the thing that
has to catch a cross-runtime `jsi::Value` escape in Release, and it will only catch it if the tests exercise the
crossing.

### 4.2 `PlatformDepMethodsHolder` does not compile, and nothing upstream stops it regressing

Section 3.1. We can carry a patch — and we will have to, to build at all — but a header patch against a package
that ships 99 platform conditionals and no third-platform CI is a maintenance liability measured per release, on top
of ADR-0001's already-accepted three-to-six-minor lag. The mitigation is the RFC in Appendix A, and specifically the
compile-only CI job it asks for; a vendored patch without that is a treadmill.

### 4.3 Frame-clock coupling and the 120 Hz budget

`FrameClock` gates drawing: `onFrameCallback(now)` always draws, `onFallbackTimeout(now, hasPendingWork)` draws only
if there is pending work, computed from `LinuxMountingManager::hasPendingDamage()`, `ScrollController::isScrollActive()`
and `ReactHost::hasPendingTimers()` (*Frame clock (#59)*, `docs/cpp-toolchain.md`). A running native-driven animation
is a **fourth** pending-work source and it is not in that OR today. Get it wrong in the permissive direction and an
occluded window spins the GPU forever; get it wrong in the strict direction and an animation freezes the moment
Hyprland stops sending frame callbacks for a background workspace — which is #59's entire thesis, restated for
animation.

`AnimationChoreographer::resume()`/`pause()` map onto exactly this: `resume` means "the backend has work, treat that
as pending", `pause` means "it does not". `AnimationBackend` calls them through `isRenderCallbackStarted_` [V], and
Reanimated's `stopBackendIfIdle` computes a seven-term `hasWork` before stopping [V] — a good model for ours.

At 120 Hz the frame budget is 8.33 ms *including* compositor time, and per ADR-0001 the real client share is
unspecified. A frame now potentially contains: the choreographer callback, the animated-graph evaluation, Reanimated's
grand callback (JS in the UI runtime), prop application, possibly a Fabric commit with Yoga relayout, the scene
snapshot under the mounting mutex, and the Skia paint. Any per-frame Fabric commit at 120 Hz is a design error, which
is why `synchronouslyUpdateViewOnUIThread` is a Phase 1 P0 rather than an optimisation.

### 4.4 Silent flag divergence

Three independent flag systems now decide behaviour: React Native's `ReactNativeFeatureFlags`
(`cxxNativeAnimatedEnabled`, `useSharedAnimatedBackend`, `optimizedAnimatedPropUpdates`, `enableLayoutAnimationsOn*`),
worklets' `WORKLETS_FEATURE_FLAGS` compile definition, and Reanimated's `REANIMATED_FEATURE_FLAGS` string parsed at
compile time by `constexpr` [V]. A wrong or missing string does not fail the build; it changes which code path runs.
Every one of them belongs in a single asserted place with a test that reads it back.

### 4.5 The JavaScript fallback is thin

Section 3.5. One consumer, one contributor, a regression that shipped, and a fix merged the day before this report.
If Phase 2's decision is "take the fallback first", the plan must budget for maintaining our own Metro resolver
shim, and must not assume `Platform.OS === 'linux'` will ever be a case Software Mansion carries.

### 4.6 Scope collisions inside our own tracker

M2 already holds #19 (native-driven animations), #59 (frame-source liveness), #74 (unmounted-target hang), #75
(Animated value conformance), #20 (pacing), #58 (upstream-parity drift oracle), and `scripts/issue-plans/core.json`
adds five more `test(animation):`/`perf(animation):` issues at M2. This plan places implementation at M4. See
*Open questions*, item 1 — that inversion is real and needs a decision, not a footnote.

---

## 5. The three phases

All 19 issues are sub-issues of #1 under **M4**, per the decision recorded in #95.

### Phase 1 — `Animated` with `useNativeDriver: true` on the C++ animation backend

Integration, not implementation. Install a TurboModule binding and register the in-tree C++ `AnimatedModule`; add
`react/renderer/animated` to the build; enable `cxxNativeAnimatedEnabled` and `useSharedAnimatedBackend`; implement
`LinuxAnimationChoreographer` over `FrameClock`; implement `synchronouslyUpdateViewOnUIThread` so a non-layout frame
never commits; then event-driven `Animated.event`. Adopt upstream's own `react/renderer/animated/tests` as a pinned
drift oracle.

Behavioural conformance for this phase is **already planned** in `scripts/issue-plans/core.json` at M2 — *"the
animated value and the shadow tree agree on every frame, not only at the end"* (P0), *"the native-driver style
allowlist is enforced at the boundary"*, *"LayoutAnimation — decide it, then prove it or refuse it"*, *"a
native-driver frame-cost gate"*, and *"hit-testing during a running animation — the pressed node is the painted
node"* (P0). Phase 1 is the implementation those tests gate; `animation.json` does not restate them.

### Phase 2 — the worklets runtime on Hermes

Opens with a `needs:decision` research issue on the JavaScript-fallback rung, because that answer changes how much of
Phase 2 is on the critical path. Then: build `Common/cpp` for Linux with the right defines; `LinuxUIScheduler` and the
UI runtime, carrying the threading decision from *Risks* 4.1; the `WorkletsModule` TurboModule host wiring
`RuntimeBindings::requestAnimationFrame` to `FrameClock`; the Babel plugin, Metro resolution and autolinking; and a
conformance suite over serialization, `runOnUI`/`runOnJS` round-trips and the runtime-per-thread contract, TSan-clean.

### Phase 3 — Reanimated 4 on Fabric

`PlatformDepMethodsHolderImpl` for Linux — including the patch that makes the header compile at all; the
`ReanimatedModuleProxy` host with the commit hook, mount hook and props registries riding `UIManagerAnimationBackend`;
CSS animations and transitions; layout animations and the `LayoutAnimationsProxy`; the platform stubs (keyboard,
sensors, reduced motion, pseudo-selectors, gesture state); a conformance suite plus the flagship animation gate. The
upstream RFC to Software Mansion is filed as its own issue, drafted in Appendix A, and opened only on explicit go.

---

## 6. Open questions for review

1. **Milestone.** #95 places all of this at M4, but Phase 1's conformance tests sit at M2 (`core.json`) alongside
   #19, #59, #74, #75. Either Phase 1's six issues move to M2, or those tests move to M4. Left as decided (M4) and
   flagged rather than silently re-milestoned.
2. **#19's scope.** *"feat(core): native-driven animations on the render thread"* was written before we knew RN
   ships the C++ graph. It should either be closed in favour of Phase 1's issues or narrowed to the choreographer.
   Not touched here.
3. **The threading decision (4.1)** is probably an ADR amendment, not an issue. Phase 2 currently carries it inside
   `feat(modules): LinuxUIScheduler and the worklets UI runtime on the frame thread`.
4. **Vendoring policy.** Reanimated must be patched to compile. Vendored fork, a patch file applied at install, or
   an upstream-first block? Not decided here.
5. **Whether the JS fallback ships to users** or is only an internal bring-up rung. Phase 2's first issue asks the
   question; the answer changes Phase 3's priority, not its content.
6. **`platform-parity:*` labelling.** Neither iOS nor Android nor Windows is a clean oracle here — Windows is a
   *counter*-oracle. `platform-parity:ios` is used where iOS behaviour genuinely is the contract (`Animated`
   semantics, Reanimated's apple implementation) and omitted elsewhere.

---

## Appendix A — draft RFC to Software Mansion

**Not sent. No outreach without explicit go.** Intended venue: a discussion on
`software-mansion/react-native-reanimated`, referencing
[#10117](https://github.com/software-mansion/react-native-reanimated/pull/10117),
[#4917](https://github.com/software-mansion/react-native-reanimated/pull/4917) and
[#4669](https://github.com/software-mansion/react-native-reanimated/pull/4669).

---

### RFC: a supported contract for a third platform in Worklets and Reanimated

#### Who is asking

`react-native-linux` is an out-of-tree React Native platform for desktop Linux: New Architecture only, Fabric
mounted into a Skia/Vulkan scene on a Wayland surface, Hermes built from source, React Native 0.87.1. We are not
asking anyone to maintain a Linux platform. We are asking for the shared C++ to keep compiling on a platform that is
neither Android nor Apple, and for the seams to be nameable.

#### What we found when we tried

We read `react-native-worklets` 0.12.1 and `react-native-reanimated` 4.6.0 against a platform that defines neither
`ANDROID` nor `__APPLE__`.

**Worklets is nearly there.** Fifteen conditional-compilation sites in `Common/cpp`, every one in logging, tracing,
or the opt-in fetch preview, every one degrading to a safe no-op. The platform contract is small and well factored:
`UIScheduler` with one pure virtual (`queryIsOnUIThread`), `RuntimeBindings::requestAnimationFrame`, five static
`PlatformLogger::log` overloads, and a TurboModule host that constructs `WorkletsModuleProxy`. `ScriptLoader` is
already header-only and portable. This is a good design and it deserves to be stated as a supported one.

**Reanimated stops at the first header.** `Common/cpp/reanimated/Tools/PlatformDepMethodsHolder.h` defines
`SynchronouslyUpdateUIPropsFunction` under `#ifdef ANDROID` / `#elif __APPLE__` with no `#else`, then declares a
required struct member of that type. The struct does not compile on a third platform. Behind it are 99
`ANDROID`/`__APPLE__` sites in `Common/cpp`, about 44 of them in `LayoutAnimations/`.

We also note the direction of travel and want to support it: `canRouteCSSProperty` and
`shouldUseSynchronousUpdatesInPerformOperations` already carry generic `#else` branches with honest comments, and
`PlatformDepMethodsHolder`'s last member is ordered *"so a platform that does not supply it can omit it and get
value-init."* The generic branch is already a concept in this codebase. It is just not enforced.

#### What we are asking for

1. **Give `SynchronouslyUpdateUIPropsFunction` an `#else`.** The Apple form — `std::function<void(const int, const
   folly::dynamic &)>` — is the portable one and matches React Native's own
   `IMountingManager::synchronouslyUpdateViewOnUIThread(Tag, const folly::dynamic&)`. One branch unblocks the whole
   header. This is the single highest-leverage change in this document.
2. **Add a compile-only CI job for a generic target** — no `ANDROID`, no `__APPLE__`, C++20, Hermes, `ReactCommon`
   headers — that builds `Common/cpp` for Worklets and Reanimated. Not a test suite, not a platform: a guard so that
   the `#ifdef` pairs you are already writing generic branches for cannot silently regress. We would contribute and
   help maintain it.
3. **Name the platform contract.** A short `docs/` page listing what a platform must provide (`UIScheduler`,
   `requestAnimationFrame`, `PlatformLogger`, the TurboModule host, `PlatformDepMethodsHolder`) and what it may omit
   (sensors, keyboard, pseudo-selectors, shared transitions, platform CSS routing). Marking the optional members
   `std::function` value-initialised and null-checked at the call site — as `platformAnimationFactory` already
   anticipates — makes "omit it" a supported answer rather than a crash.
4. **Declare React Native's shared animation backend the supported prop path for platforms without UIKit or Android
   views.** Reanimated 4.6 already routes its whole frame loop through `UIManagerAnimationBackend::start` under
   `USE_ANIMATION_BACKEND` on RN ≥ 0.85. For a platform whose renderer is a canvas, that path is not an optimisation
   — it is the only one that exists. Making `USE_ANIMATION_BACKEND` reachable off Android and Apple, and treating the
   backend rather than `synchronouslyUpdateUIProps` as the primary contract, would let a new platform implement one
   seam (`AnimationChoreographer`) instead of two.
5. **Keep the JavaScript fallback, and key it on capability rather than on `Platform.OS === 'windows'`.** #10117
   restored a real, useful rung: a native platform with no Worklets module running the web implementation. Please
   keep it. If the Metro resolver check in `wrapWithReanimatedMetroConfig` keyed off "this platform has no native
   Worklets module" instead of a hardcoded platform name, every future out-of-tree platform would inherit a working
   fallback on day one, and the regression that #10117 fixed would have been impossible.
6. **Accept a platform key in autolinking metadata.** `codegenConfig` and `react-native.config.js` currently
   enumerate `android` and `ios`. An out-of-tree platform runs its own codegen driver either way, but a documented
   place to declare a third platform's source globs and defines would remove a class of downstream forks.

#### What we are offering

- The compile-only CI job in ask 2, and the patches to keep it green.
- A second real consumer of the generic branch, exercising it against React Native 0.87 with Hermes on desktop
  Linux — an environment React Native itself already builds and tests, via `private/react-native-fantom`'s
  `if(UNIX AND NOT APPLE)` branch and its `8-core-ubuntu` CI.
- Public, honest reporting of what does and does not work, including the parts where a canvas renderer cannot match
  UIKit or Android semantics.

#### What we are not asking for

Not a Linux platform in your repository, not a release channel, not support commitments, and not a `linux` entry in
`compatibility.json`. We expect to carry the platform, the port, and its bugs. We are asking that the shared C++
stay compilable, and that the seams have names.
