# Test-suite parity inventory — 2026-09-05

Inventory for epic #209 (test-suite parity): what React Native and its desktop platforms ship as automated
tests, what we already run, and which of the rest we should link unmodified, port, or skip. Every count below
was produced by listing the vendored tree at `scripts/vendor.lock.json`'s tag (`v0.87.1`), by
`packages/core/tests/CMakeLists.txt`, or by a read-only `gh api` call against the named repository; where a
call failed, the row says so.

Two references #209 makes are not in the repository: `docs/research/test-suite-parity.md` and
`scripts/issue-plans/test-suites.json` do not exist (`docs/research/` holds eight other surveys;
`scripts/issue-plans/` holds `animation`, `core`, `ecosystem`, `macos`, `windows`). This file is the inventory
that name was pointing at.

Two upstream paths named in the brief do not exist either: `vnext/Microsoft.ReactNative.Cpp.UnitTests` in
react-native-windows (the directory is `vnext/Microsoft.ReactNative.Cxx.UnitTests`), and
`packages/react-native/jest` in react-native at `v0.87.1` (the jest setup lives at `packages/jest-preset/jest/`:
`setup.js`, `mocks/`, `react-native-env.js`, `resolver.js`, `renderer.js`, `MockNativeMethods.js`).

## A. What we run today

| Suite | Comes from | Linked / ported / ours | Count |
| --- | --- | --- | --- |
| `rnl_core_tests`, our own files | `packages/core/tests/*Test.cpp` (30 files) | ours | 484 `TEST`/`TEST_F` macros; `SceneTest` 101, `EditorTest` 53, `ScrollTest` 43, `InputTest` 31, `ImageTest` 31, `LayoutConformanceTest` 20, `ImeTest` 18, `FocusTest` 17, `AnimatedHitTestTest` 16, `AnimatedPropsTest` 15, the rest under 13 each |
| Upstream animated suite (#132) | `ReactCommon/react/renderer/animated/tests` (5 `.cpp` + `AnimationTestsBase.h`) | linked unmodified | 22 macros |
| Upstream css suite (#211) | `ReactCommon/react/renderer/css/tests` (18 files) | linked unmodified | 312 macros |
| Upstream core suite, standalone half (#211) | `ReactCommon/react/renderer/core/tests` — 5 of 13 `.cpp` (`ComponentDescriptor`, `DynamicPropsUtilities`, `Primitives`, `PropsConcepts`, `ShadowNode`); 8 removed by `list(REMOVE_ITEM ...)` | linked unmodified | 28 of the directory's 88 macros |
| Upstream mounting suite (#211) | `ReactCommon/react/renderer/mounting/tests` — 5 of 6 (`StateReconciliationTest` removed) | linked unmodified | 25 of 32 |
| Upstream components/view suite (#211) | `ReactCommon/react/renderer/components/view/tests` (4 files) | linked unmodified | 79 |
| Upstream graphics suite (#211) | `ReactCommon/react/renderer/graphics/tests` (4 files) | linked unmodified | 20 |
| Upstream performance/timeline suite (#211) | `ReactCommon/react/performance/timeline/tests` (4 files) | linked unmodified | 20 |
| Drift oracles | `UpstreamAnimatedSuiteTest.cpp`, `UpstreamFabricSuiteTest.cpp` | ours | 2 tests asserting the file list of 7 vendored `tests/` directories |
| Coverage gate | `scripts/cpp-coverage.ts` over the `scopedSourcePaths` list | ours | 100 % line and branch on the nine scoped `packages/core/src` files; vendored upstream sources are outside the gate |
| Vitest | 27 `*.spec.ts` under `packages/cli/src` (4), `packages/core/goldens` (3), `packages/core/src-linux` (1), `scripts/**` (19) | ours | 100 % thresholds |
| Raster goldens | `packages/core/goldens/*.png` via `hello_react --golden` / `--damage-golden`, `golden.spec.ts`, `png-diff.ts` (zero tolerance) | ours | 20 raster PNG files |
| Window goldens (#33) | `packages/core/goldens/window-*.png` via lavapipe + `weston --backend=headless`, `perceptual-diff.ts` | ours | 2 PNG files (`window-fabric-view`, `window-view-props`) |
| E2E scenarios (#7) | `packages/core/e2e/*.json` under `cage`, `rnl_inject` virtual pointer/keyboard, `wp_presentation` timings | ours | 7 scenarios (`animated-frames`, `hit-paint`, `mouse-button`, `pressable`, `rounded-press`, `text-input`, `text-input-editing`); 1 e2e golden (`pressable-click.png`) |
| Fixture bundles | `packages/core/test-bundles/*.js`, hand-written against `nativeFabricUIManager` | ours | 26 bundles |
| CI | `.github/workflows/ci.yml`: `validate`, `meta`, `unit` (Hermes-free configure, ctest, coverage gate), `native` (preset matrix) and the jobs after line 366 that were not read for this survey | ours | — |

Totals for the C++ binary: 484 own macros plus 506 linked upstream macros. PR #219's landing comment reports
976 CTest cases green (parameterised tests expand under `gtest_discover_tests`).

## B. Upstream C++ suites not yet linked

The vendored `ReactCommon` has 36 `tests/` directories and `ReactCxxPlatform` has 3. Counts are `TEST*` macros
in that directory. "Hermes" in the blocker column means the file includes `<hermes/hermes.h>`, which the
Hermes-free `test` configure cannot satisfy (docs/cpp-toolchain.md hazard 3: Hermes' bundled llvh defines the
`gtest` target names). "gmock" means `BUILD_GMOCK` is `OFF` in `packages/core/tests/CMakeLists.txt`.

Every target named as "already linked" is in `RNL_REACT_COMMON_TARGETS` or `RNL_REACT_CXX_PLATFORM_SUBDIRS`
in `packages/core/CMakeLists.txt`.

### Link unmodified now — no new code beyond the CMake source list

| Path (under `ReactCommon/`) | Tests | What it proves | Relevance to Skia/Wayland | Blocker | Maps to |
| --- | --- | --- | --- | --- | --- |
| `react/renderer/core/tests/{ConcreteShadowNode,FindNodeAtPoint,LayoutableShadowNode,ShadowNodeFamily}Test.cpp` | 1+10+24+1 | Layout-based hit testing, layout metrics and measure, node family cloning | High: `FindNodeAtPoint` is the layout-side oracle our scene-side `RetainedScene::findNodeAtPoint` (#121) must agree with when no animation is in flight; `LayoutableShadowNode` is #115's contract | None. The CMake comment says all eight removed core files "include hermes/hermes.h"; only `RawProps`, `RawValue`, `EventQueueProcessor`, `EventTargets` do. These four include only `react/renderer/element/testUtils.h`, whose descriptors (`rrc_modal`, `rrc_scrollview`, `rrc_text`, `rrc_root`, `rrc_view`) are all linked already | #211 follow-up (E1) |
| `react/renderer/mounting/tests/StateReconciliationTest.cpp` | 7 | State reconciliation across commits | High: the ScrollView `contentOffset` write-back (#45, #109) and `TextInputV3State` ride this path | None; the CMake comment says `rrc_modal` is "not linked here" but it is in `RNL_REACT_COMMON_TARGETS` and #219's landing comment confirms it | E1 |
| `react/renderer/components/scrollview/tests/ScrollViewTest.cpp` | 6 | `ScrollViewState` | High | None (`rrc_scrollview` linked) | E1 |
| `react/renderer/components/root/tests/RootShadowNodeTest.cpp` | 1 | Root node layout constraints | Medium | None | E1 |
| `react/renderer/components/text/tests/BaseTextShadowNodeTest.cpp` | 3 | Attributed-string assembly from nested text | High: #112 fragment inheritance | None (`rrc_text`, `react_renderer_attributedstring` linked) | E1 |
| `react/renderer/components/image/tests/ImageTest.cpp` | 1 | Image props | Low | None | E1 |
| `react/renderer/attributedstring/tests/{AttributedStringBox,ParagraphAttributes}Test.cpp` | 9 | Value semantics of the text model | High: #111, #112 | None | E1 |
| `react/renderer/textlayoutmanager/tests/TextLayoutManagerTest.cpp` | 7 | `TextMeasureCache` keys and equality | Medium: header-level cache the Skia `TextLayoutManager` also uses; note our `TextLayoutManager.cpp` is swapped in only under Skia, so this never sees our measurer | None | E1 |
| `react/renderer/imagemanager/tests/ImageManagerTest.cpp` | 1 | `ImageManager` construction | Low: our `ImageManager` swap is Skia-conditional, so the `test` configure runs upstream's | None | E1 |
| `react/renderer/uimanager/tests/{FabricUIManager,FindShadowNodeByTag,PointerEventsProcessor}Test.cpp` | 8 | UIManager lookups, pointer-events processing and hover tracking | High: `PointerEventsProcessor` is the hover chain (#36) and `pointerEvents` (#64) contract | Includes `<jsi/jsi.h>` header-only; `jsi` target linked. If any test constructs a `Runtime`, it moves to E2 | E1 |
| `react/renderer/uimanager/consistency/tests/LazyShadowTreeRevisionConsistencyManagerTest.cpp` | 9 | Revision consistency for `measure()` reads | High: #115 | None (`react_renderer_uimanager_consistency` linked) | E1 |
| `react/renderer/runtimescheduler/tests/SchedulerPriorityTest.cpp` | part of 37 | Priority conversions | Medium | None (only `RuntimeSchedulerTest.cpp` in that directory includes Hermes) | E1 |
| `react/renderer/telemetry/tests/TransactionTelemetryTest.cpp` | 4 | Transaction timing with `MockClock` | Medium: #106 mounting cost | None (`react_renderer_telemetry`, `react/test_utils` linked) | E1 |
| `react/renderer/element/tests/ElementTest.cpp` | 1 | `ComponentBuilder` | Low | None | E1 |
| `react/renderer/mapbuffer/tests/MapBufferTest.cpp` | 22 | MapBuffer serialisation | Low (Android-facing, but platform-neutral) | None | E1 |
| `react/renderer/debug/tests/DebugStringConvertibleTest.cpp` | 10 | Debug string output | Low | None | E1 |
| `react/utils/tests/{Base64,fnv1a,hash_combine}Tests.cpp, SimpleThreadSafeCacheTest.cpp, UuidTest.cpp` | 13 | Utilities | Low | None (`react_utils` linked) | E1 |
| `react/timing/tests/PrimitivesTest.cpp` | 5 | Time primitives | Low | None (`react_timing` linked) | E1 |
| `cxxreact/tests/{jsarg_helpers,jsbigstring,methodcall}.cpp, RecoverableErrorTest.cpp` | 16 | Legacy argument helpers, `JSBigString` via `mmap` | Low | None (`react_cxxreact` linked; POSIX `mmap` exists) | E1 |
| `jserrorhandler/tests/StackTraceParserTest.cpp` | 41 | Stack-trace parsing for the error surface | Medium: #56 error naming | None (`jserrorhandler` linked) | E1 |
| `jsinspector-modern/tracing/tests/{ProfileTreeNode,RuntimeSamplingProfileTraceEventSerializer,TimeWindowedBuffer}Test.cpp` | 26 | Trace serialisation | Low until a profiler UI exists | Includes only tracing headers; `jsinspector_tracing` linked. Verify no gmock at link time | E1 |
| `ReactCxxPlatform/react/threading/tests/TaskDispatchThreadTests.cpp` | 15 | `TaskDispatchThread` shutdown and ordering | High: `react/threading` is what our JS-thread dispatch is built on; the seam #212 names | None (`react/threading` in `RNL_REACT_CXX_PLATFORM_SUBDIRS`) | E1, #212 |
| `ReactCxxPlatform/react/profiling/tests/TimeSeriesTests.cpp` | 3 | Time-series arithmetic | Low | `react/profiling` subdirectory not added; one line | E1 |

### Port or link in a Hermes-linked binary — needs a second test executable

| Path | Tests | What it proves | Relevance | Blocker | Maps to |
| --- | --- | --- | --- | --- | --- |
| `jsi/jsi/test/testlib.{h,cpp}` — `JSITestBase : TestWithParam<RuntimeFactory>`, `runtimeGenerators()` | parameterised | JSI conformance of the runtime the platform ships | Highest: RNW ran it (`vnext/ReactCommon.UnitTests/JsiRuntimeGenerators.cpp`) and then disabled the job ("Bug #8000") | Needs a `RuntimeFactory` returning Hermes; Hermes and googletest cannot share the current `test` configure | E2 |
| `react/runtime/tests/cxx/{ReactInstance,RuntimeExecutorShutdown}Test.cpp` | — | `ReactInstance` lifecycle; runtime executor shutdown | Highest: #76 teardown leak gate, #77 cross-thread promise, #171 `TimerManager` handles, #212 | Hermes + gmock | E2, #212 |
| `react/bridging/tests/{Bridging,Class}Test.cpp` (+`BridgingTest.h`) | 32 | JSI bridging conversions, `TestCallInvoker` | High: TurboModule return paths (#21, #148, #77) | Hermes (`BridgingTest.h`) | E2 |
| `react/renderer/core/tests/{RawProps,RawValue,EventQueueProcessor,EventTargets}Test*.cpp` | 16+1+5+2 | Props parsing from JSI, event queue ordering | High: `EventQueueProcessor` ordering is the per-frame event beat (#7, Input section) | Hermes | E2 |
| `react/renderer/runtimescheduler/tests/RuntimeSchedulerTest.cpp` | part of 37 | Real `std::thread` cross-thread scheduling against `StubClock` | High: #212 pattern | Hermes | E2, #212 |
| `react/renderer/scheduler/tests/SchedulerDelegateInvalidationTest.cpp` | 7 | Delegate invalidation on teardown | High: `LinuxMountingManager` teardown (#76) | Hermes | E2 |
| `jsinspector-modern/tests/*` (16 `.cpp` + `engines/`) | 188 | CDP plumbing, packager connection, console API, `JsiIntegrationTest` | Medium until #79 (Metro dev server) lands | Hermes + gmock + `ReactNativeMocks` | E3 |
| `ReactCxxPlatform/react/io/tests/NetworkingModuleTests.cpp` | 2 | `NetworkingModule` | Belongs with #79 | Hermes; `react/io` not built | #79 |

### Skip, with reason

| Path | Tests | Reason |
| --- | --- | --- |
| `react/featureflags/tests/*` | 13 | Already excluded in CMake: `DynamicProviderTest` asserts fresh global flag state that `ReactNativeFeatureFlagsOverridesLinux` and our `FeatureFlagsTest` mutate. Note `gtest_discover_tests` already runs each CTest case in its own process, so the conflict is only in the single-process coverage run; a `--gtest_filter` on that run would admit them, but the exclusion reason is honest and the tests cover upstream defaults we override on purpose |
| `react/renderer/animations/tests/{LayoutAnimation,MutationComparator}Test.cpp` | 22 | `LayoutAnimation` is undecided (#123, `needs:decision`); `react/renderer/animations` is not built. Link only if #123 decides to implement it (E11) |
| `react/debug/redbox/tests/*` | 42 | We have no RedBox; `react/debug/redbox` is not built. Revisit with the error surface (#56) |
| `reactperflogger/fusebox/tests/FuseboxTracerTest.cpp` | 7 | Fusebox is the DevTools side of #79; joins E3 |
| `callinvoker/ReactCommon/tests/TestCallInvoker.h`, `react/nativemodule/core/tests/TurboModuleTestFixture.h` | 0 | Fixtures, not suites; consumed by the bridging and io tests above |
| `react/renderer/core/tests/benchmarks/` | — | Benchmarks, not tests; #106/#124 own the perf gates |

## C. Upstream JavaScript suites and RNTester pages

None of these run here yet, for one reason: no React bundle boots in this host. Every fixture under
`packages/core/test-bundles` talks to `nativeFabricUIManager` directly, and nothing in `scripts/e2e.ts` bundles
through Metro (#213's blocked comment states this). The JS runtime bring-up is owned by #24 (flagship
bring-up), with #22 (Metro `--platform linux` registration) and #210 (the Fantom-style headless runner) as the
two harnesses that would execute the suites.

The vendored tree contains `packages/react-native/src` but not `Libraries` or `rn-tester`, so those counts come
from `gh api repos/facebook/react-native/git/trees/v0.87.1`.

| Suite | Where | Count | What it proves | Recommendation | Owner |
| --- | --- | --- | --- | --- | --- |
| Fantom `*-itest.js` corpus | repo-wide at `v0.87.1` | 185 files: `Libraries/` 105 (Components 21, StyleSheet 18, Utilities 17, LogBox 14, Animated 12, Image 6, ReactNative 4, Lists 3, Core 3, Text 2, Blob 2, one each of WebSocket/vendor/Pressability/Network/NativeComponent/Modal/BatchedBridge), `src/private/` 53 (webapis/dom 12, webapis/performance 7, setup 5, renderer/core 4, ...), `react-native-fantom/src/__tests__` 21, `polyfills` 2 | Mount-tree semantics against real Hermes + Fabric; `View-itest`, `ScrollView-itest`, `TextInput-itest`, `Pressable-itest`, `Switch-itest`, `Touchable*-itest`, the 18 StyleSheet `process*` tests | Run unmodified through #210; the `src/private` 53 are already on disk | #210 |
| Jest `*-test.js` under `Libraries/**/__tests__` | `Libraries/` | 31 non-itest test files (plus 2 PNG fixtures and snapshots); by area: LogBox 8, Blob 4, Components 4 (`AccessibilityInfo`, `DrawerAndroid`, `Keyboard`, `StatusBar`), Animated 3, Core 3, Utilities 2, one each of `Pressability`, `FabricUIManager`, `XMLHttpRequest`, `NativeModules`, `resolveAssetSource` | Pure-JS contracts with the `packages/jest-preset/jest/setup.js` mock layer | Run unmodified with a `linux` haste platform; `Pressability-test.js` (36 cases) first | #217, #215 |
| `src/private` jest | vendored | 2 files (`featureflags/__tests__`, `animated/__tests__`) | Flag defaults; animated JS side | With #217 | #217 |
| RNTester example pages | `packages/rn-tester/js/examples/` | 81 example directories, 241 files; `RNTesterList.android.js` registers 27 Components and 51 APIs (Accessibility, Animated, AnimationBackend, Appearance, Border, Dimensions, Filter, LinearGradient, LayoutConformance, PointerEvents, RTL, Transform, ...) | Every component and prop surface upstream considers public | Bundle for `platform=linux`; `visitAllPages` under the compositor; `snapshotPages` in-process | #213 |
| RNTester Maestro flows | `packages/rn-tester/.maestro/*.yml` | 12 flows at `v0.87.1` (`button`, `flatlist`, `image`, `image-blur-prefetch`, `image-getsize-local-drawables`, `image-progressive-jpeg`, `image-wide-gamut`, `legacy-native-module`, `modal`, `new-arch-examples`, `pressable`, `text`) plus `helpers/{launch-app-and-search,search}.yml` and 2 Android screenshots. #209's body says 40; the tree at this tag has 12 | Search-then-tap navigation and screenshot per flow | Port each as a `packages/core/e2e/*.json` scenario once RNTester boots | E6, #213 |
| RN core `IntegrationTests` (JS harness bundles) | seen in react-native-macos `packages/rn-tester/IntegrationTests/` | 14 files (`AppEventsTest`, `ImageCachePolicyTest`, `ImageSnapshotTest`, `LayoutEventsTest`, `PromiseTest`, `SimpleSnapshotTest`, `SyncMethodTest`, `TimersTest`, `WebSocketTest`, `GlobalEvalWithSourceUrlTest`, `AccessibilityManagerTest`, `LoggingTestModule`, `IntegrationTestHarnessTest`, `IntegrationTestsApp`) | A bundle asserts and calls `TestModule.markTestPassed` | Port the platform-neutral ones (`LayoutEvents`, `Timers`, `Promise`, `AppEvents`, `SyncMethod`) as `markTestPassed` bundles | E10, #214 |

## D. Desktop-platform test patterns worth adopting

### react-native-windows

`packages/e2e-test-app-fabric/test/` (listed 2026-09-05): 24 component test files (`Accessibility`, `ActivityIndicator`,
`Button`, `CustomAccessibility`, `FlatList`, `HangSimulation`, `HitTest`, `Image`, `LegacyControlStyle`,
`LegacyImage`, `LegacySelectableText`, `LegacyTextHitTest`, `LegacyTextInput`, `PointerButton`,
`PointerClickEvents`, `Pressable`, `ScrollView`, `Switch`, `Text`, `TextInput`, `Touchable`, `View`, plus
`visitAllPages.test.ts` and `snapshotPages.test.js`), helpers `Helpers.ts`, `RNTesterNavigation.ts`,
`NativePerfHelpers.ts`, and directories `__snapshots__` (21 `.snap`), `__image_snapshots__` (6 PNG files, all from
`ViewComponentTest`), `__perf__`, `__native_perf__`.

| Pattern | What it proves | Already planned? |
| --- | --- | --- |
| `visitAllPages.test.ts`: iterate `RNTesterList.Components`, `goToComponentExample(title)`, `verifyNoErrorLogs()` in `afterEach`; skip list is three named entries (`Flyout`, `XAML`, `SwipeableCard`) and the APIs loop is commented out "until stable" | Every component page mounts without a logged error | Yes: #213 (walk) + #214 (`ListErrors`). The "no silent skip list" rule in #213's acceptance is the lesson from their commented-out APIs loop |
| `snapshotPages.test.js`: `react-test-renderer` `create(<Example />)` per example, `toMatchSnapshot()`, `jest.useFakeTimers()`; skips `New App Screen` and `Image` and four named examples with an issue link | Fixture drift before any GPU time | Yes: #213 third bullet. It needs only React + Metro, not the compositor or #214, so it can land first (E5) |
| `RNTesterNavigation.ts`: click `components-tab`, type the first 8 characters of the example title into `explorer_search` with a retry loop, click the `testID` equal to the title | testID-addressable navigation | Yes: #213 names it; the addressing needs #216's tree or #214's `DumpAccessibilityTree` |
| `PressableComponentTest.test.ts`: `dumpVisualTree(testID)` → `toMatchSnapshot()` before and after `click()` | State change asserted structurally, not by pixels | Yes: #216 |
| `Helpers.ts` `verifyElementVisualSnapshot(component)`: `getLocation()`+`getSize()`, `createScreenshot({location})`, `toMatchImageSnapshot({failureThreshold: 0.01, failureThresholdType: 'percent'})` | Per-element image golden cropped to the node's frame | Partly: #214 `TakeScreenshot` gives the capture; cropping to a node's frame is not planned (E8) |
| `HangSimulationTest.test.ts`: `describe.skip` unless `RNW_SIMULATE_HANG=1`; invokes `HangForTesting`, then a UIA query blocks until jest's timeout; the pipeline's post-failure dump step is what is being tested | The forensics path itself is tested | Yes: #214 "forensics meta-test" |
| `__perf__` / `__native_perf__` with `NativePerfHelpers.ts` | Per-scenario perf numbers | Yes: #106, #124, #126 |

C++ side (listed): `vnext/Microsoft.ReactNative.Cxx.UnitTests/` (`JSValueReaderTest`, `JSValueTest`, `JsiTest`,
`NativeModuleTest`, `NoAttributeNativeModuleTest`, `ReactContextTest`, `ReactPromiseTest`, `TurboModuleTest`,
`ReactModuleBuilderMock`), `vnext/ReactCommon.UnitTests/` (`JsiRuntimeGenerators.cpp` — the upstream JSI
conformance suite's `runtimeGenerators()` for their runtimes; the job is disabled per #211), `vnext/Desktop.UnitTests/`
(`LayoutAnimationTests`, `MemoryMappedBufferTests`, `ScriptStoreTests`, `UnicodeConversionTest`, HTTP filter
and WebSocket mocks), `vnext/Desktop.IntegrationTests/` (`RNTesterHeadlessTests.cpp`, `HttpResourceIntegrationTests`,
`WebSocketIntegrationTest`, `Modules/Test{AppState,DevSettingsModule,DeviceInfo,ImageLoaderModule}`),
`vnext/Microsoft.ReactNative.IntegrationTests/` (`JsiRuntimeTests`, `JsiTurboModuleTests`,
`JsiSimpleTurboModuleTests`, `ReactNativeHostTests`, `ReactPropertyBagTests`, `ReactNotificationServiceTests`,
`TurboModuleTests`), and `vnext/Mso.UnitTests/` (dispatchQueue, future, activeObject, eventWaitHandle, errorCode).
A recursive search for any file matching `test` under `vnext/Microsoft.ReactNative/` returned nothing: the
Fabric composition layer has no C++ unit tests, as #209 says.

What to take from the C++ side: `RNTesterHeadlessTests.cpp`'s shape (host loads a bundle, JS calls
`markTestPassed`, C++ awaits) is #214's `MarkTestPassed`; the `JsiTurboModuleTests` shape (a C++ TurboModule
registered for the test, a JS bundle that calls it, an assertion on the round trip) is what #148's generated
registration should be proven with. `Mso.UnitTests` is #212's reference and #212 is in review.

### react-native-macos

`packages/rn-tester/` listing: `RNTester-macOSUnitTests/RNTesterUnitTests_macOS.m` is an `XCTestCase` with
`setUp`/`tearDown` and no test methods; `RNTester-macOSIntegrationTests/RNTesterIntegrationTests_macOS.m` has
one empty `testExample` that launches the app. The shared `RNTesterUnitTests/` has 35 iOS-era `.m` files
(`RCTShadowViewTests`, `RCTUIManagerTests`, `RCTNativeAnimatedNodesManagerTests`, `RCTComponentPropsTests`,
`RCTEventDispatcherTests`, `RCTFontTests`, ...); `RNTesterIntegrationTests/` has `RCTLoggingTests`,
`RCTUIManagerScenarioTests`, `RNTesterIntegrationTests` and `ReferenceImages/`; `.maestro/` has 8 flows
(`button`, `flatlist`, `image`, `legacy-native-module`, `modal`, `new-arch-examples`, `pressable`, `text`).

The pattern to adopt is the negative one #218 already states: a desktop-only behaviour with no assertion in the
same PR is the macOS shape. Nothing macOS-specific is portable; the shared `IntegrationTests/*.js` bundles are
(section C, last row).

### react-native-skia (react-native-skia/react-native-skia)

No test suite. `testing/` is Chromium's build tooling (`gtest/`, `xvfb.py`, `PRESUBMIT.py`). The repository's
own checks are manual JS apps under `packages/react-native-skia/`: `MutationTest.js`,
`components/image/ImageResizeModeTest.js`, `components/image/ShadowUnitTestApp.js` with a
`ShadowUnitTestApp_Reference_Output/` folder of six PNG files (shadow on view, on transformed view, on PNG and
JPEG images, with resize mode), `components/text/ParagraphTestApp.js`, `components/transform/TransformTestCases.js`,
`components/view/ViewStylePropsTestApp.js`, `modules/Networking/Networking-test*.js`. The reference-output
folder is a hand-compared golden; our `packages/core/goldens` already does this with a comparator. The
`TransformTestCases.js` list is worth reading when #104 (transformOrigin/perspective) picks its fixtures.

## E. Proposed new sub-issues under #209

Each entry: title, context, acceptance criteria naming the test layer, and a DEDUPE line. Priorities follow the
board's labels.

### E1. `test(core): link the remaining Hermes-free upstream suites — second #211 tranche, no new code`

Context: #211 landed the first tranche (PR #219) and closed; its landing comment defers "the jsi conformance /
jsinspector / cxxreact / bridging tranches on this same issue", which a closed issue cannot carry. Section B's
first table lists 24 files with roughly 250 `TEST` macros whose every dependency is already in
`RNL_REACT_COMMON_TARGETS`: the four core files the CMake comment misattributes to Hermes
(`ConcreteShadowNode`, `FindNodeAtPoint`, `LayoutableShadowNode`, `ShadowNodeFamily` include only
`react/renderer/element/testUtils.h`), `StateReconciliationTest` (its stated blocker `rrc_modal` is linked),
scrollview, root, text, image, attributedstring, textlayoutmanager, imagemanager, uimanager (3), consistency,
`SchedulerPriorityTest`, telemetry, element, mapbuffer, debug, utils, timing, cxxreact, jserrorhandler,
jsinspector tracing, and `ReactCxxPlatform/react/threading/tests/TaskDispatchThreadTests.cpp` (plus
`react/profiling` behind one `add_subdirectory`).
Acceptance: unit — every file compiles into `rnl_core_tests` from its upstream path and is green under
`ctest --preset test` and the coverage run; `UpstreamFabricSuiteTest.cpp` gains each directory's file list; the
two wrong CMake comments are corrected; any file that turns out to need a `Runtime` is moved to E2 with the
reason written in CMake. No golden or e2e layer applies.
DEDUPE: #211 (closed; this is its deferred tranche), #212 (`TaskDispatchThreadTests` is one of its seams),
#58 (the oracle ritual, not the suites).

### E2. `test(core): a Hermes-linked upstream test binary — JSI conformance and the runtime-bound suites`

Context: `jsi/jsi/test/testlib.h` is upstream's JSI conformance suite, parameterised on a `RuntimeFactory`;
react-native-windows wired it (`vnext/ReactCommon.UnitTests/JsiRuntimeGenerators.cpp`) and then disabled the
job (#211 quotes "Bug #8000"). Ten more upstream files include `<hermes/hermes.h>`: `react/runtime/tests/cxx/
{ReactInstance,RuntimeExecutorShutdown}Test.cpp`, `react/bridging/tests/*`, core's `RawProps`, `RawValue`,
`EventQueueProcessor`, `EventTargets`, `RuntimeSchedulerTest`, `SchedulerDelegateInvalidationTest`. Hazard 3 in
docs/cpp-toolchain.md is why they cannot join `rnl_core_tests`: Hermes' llvh owns the `gtest` target names in a
Hermes configure. The minimal answer is a second executable in the default (Hermes) configure that links
Hermes' own bundled googletest rather than fetching another, or the #210 tester binary hosting them.
Acceptance: unit — `rnl_core_hermes_tests` (or the #210 binary) runs `testlib` with a Hermes factory and the
ten files unmodified, green in the `native` CI matrix including ASan/UBSan and TSan; the file lists join the
drift oracle. `RuntimeExecutorShutdownTest` is the generalised #171 contract test. No golden or e2e layer.
DEDUPE: #211's deferred jsi tranche; #210 (its binary is the natural host); #212, #76, #77, #171 (the
shutdown contracts these suites assert).

### E3. `test(core): jsinspector-modern suite with gmock — the CDP and Fusebox drift guard`

Context: `jsinspector-modern/tests/` holds 16 `.cpp` files and 188 `TEST` macros (`HostTargetTest`,
`InspectorPackagerConnectionTest`, `ConsoleApiTest`, `NetworkReporterTest`, `TracingTest`, `JsiIntegrationTest`
with `engines/` adapters) plus `reactperflogger/fusebox/tests/FuseboxTracerTest.cpp` (7). They need gmock
(`BUILD_GMOCK` is `OFF` today) and, for `JsiIntegrationTest`, Hermes. This is the suite that makes #79's Metro
dev server and DevTools connection a tested seam rather than a hoped-for one.
Acceptance: unit — the suite compiles into the E2 binary with gmock enabled, green under sanitizers; excluded
files carry a reason in CMake. Lands with, or immediately after, #79.
DEDUPE: E2 (same binary), #79 (the feature it guards).

### E4. `test(core): the drift oracle covers every vendored tests directory, not six`

Context: `UpstreamFabricSuiteTest.cpp` asserts the file lists of six directories; the vendored tree has 36 under
`ReactCommon` and 3 under `ReactCxxPlatform`. A bump that adds a test file to `runtimescheduler/tests` or
`react/runtime/tests/cxx` today changes nothing we notice. #58 wants a bump ritual; the ritual needs the oracle
to see every directory, including the ones we deliberately do not link, so an exclusion is a reviewed line and
not an unseen one.
Acceptance: unit — one table in the oracle lists all 39 directories with their file lists and a
`linked`/`excluded(reason)` marker; the test fails on any add or remove; docs/cpp-toolchain.md's Pins section
names it as the bump gate.
DEDUPE: #58 (the ritual; this is its precondition), #179 (upstream-sync hygiene, which checks the lock not the
suites).

### E5. `test(harness): snapshotPages — every RNTester example rendered in-process, before any compositor`

Context: RNW's `packages/e2e-test-app-fabric/test/snapshotPages.test.js` renders every `RNTesterList` example
through `react-test-renderer` under fake timers and snapshots the JSON tree. It needs React and Metro in the
repository (#22, #24) and nothing else: no compositor, no #214 channel. #213 lists it as its third bullet
behind two blockers that do not apply to this rung, so splitting it out lets it land first and gives #213's
`visitAllPages` a known-good page list to walk.
Acceptance: unit (Vitest, 100 % gate on our runner code) — a `linux` resolution of `RNTesterList`, one
snapshot per example, skip list empty or each entry linked to an issue; runs in the `validate` job.
DEDUPE: #213 (this is its in-process bullet, pulled forward), #217 (shares the `linux` haste platform).

### E6. `test(harness): the twelve upstream Maestro flows as e2e scenarios`

Context: `packages/rn-tester/.maestro/` at `v0.87.1` holds 12 flows (`button`, `flatlist`, `image` and four
image variants, `legacy-native-module`, `modal`, `new-arch-examples`, `pressable`, `text`) built on
`helpers/launch-app-and-search.yml`: launch, type into the search box, tap the example, assert visible text,
screenshot. Our `packages/core/e2e/*.json` schema already expresses launch, `move`/`click`/`key`, trace
expectations and a screenshot golden; the helper maps onto RNW's `RNTesterNavigation` search pattern.
Acceptance: e2e — one scenario per flow under `cage` once RNTester boots (#213), each with a screenshot golden
under `packages/core/e2e/goldens/` and trace expectations; flows that need an unimplemented module (`modal`
until #62 decides, `legacy-native-module`) are listed with their owning issue rather than skipped silently.
DEDUPE: #213 (page walk and per-example goldens; this is the interaction subset), #7 (driver).

### E7. `test(harness): every e2e scenario fails on a logged error by default`

Context: RNW calls `verifyNoErrorLogs()` in `afterEach` of every e2e file and `visitAllPages` relies on it as
its only assertion. Our scenarios assert only the substrings in `expect`; a `console.error` or a native
diagnostic line in the trace passes unless a scenario happens to name it. Until #214's `ListErrors` channel
exists, the trace `scripts/e2e.ts` already captures is the source: a scenario-level `allowErrors` defaulting to
false, graded in `scripts/e2e/grade.ts`.
Acceptance: unit — `scenario.spec.ts` and `grade.spec.ts` cover the new field at 100 %; e2e — all seven current
scenarios pass with the default, and a deliberately erroring fixture (`throws.js` exists in `test-bundles`)
fails.
DEDUPE: #214 (`ListErrors` replaces the trace source later; the scenario field stays).

### E8. `test(harness): per-element screenshot goldens cropped to a node's frame`

Context: RNW's `Helpers.ts` `verifyElementVisualSnapshot` screenshots only the element's rectangle
(`getLocation` + `getSize`, `failureThreshold: 0.01` percent), which is why their six image snapshots stay
stable while the rest of the page changes. Our e2e screenshot compares the full 800x600 surface, so any
unrelated change on the page invalidates `pressable-click.png`. The frame comes from the shadow tree by
`testID` (the lookup `InputDispatcher` already does by tag) or from #216's tree dump.
Acceptance: unit — the crop and comparator in `scripts/e2e/screenshot.ts` at 100 %; e2e — `pressable.json`
gains a `screenshot.testID` and its golden shrinks to the card; golden — none new.
DEDUPE: #214 (`TakeScreenshot`), #216 (the node addressing), #7 (comparator note "one comparator, not two").

### E9. `test(input): FindNodeAtPoint and RetainedScene::findNodeAtPoint agree on every static tree`

Context: hit testing reads the painted scene (#121, docs/cpp-toolchain.md "Hit-testing under animation") while
upstream's `react/renderer/core/tests/FindNodeAtPointTest.cpp` (10 tests) asserts the layout-side algorithm.
When no animation is in flight the two must return the same node; #35's proof compares hit against paint, not
against upstream's answer. Once E1 links the upstream file, a differential test that commits each of its trees
through `ShadowTree`, mounts into `RetainedScene`, and asks both hit-testers the same points is the oracle for
`pointerEvents` (#64), transforms (#103) and clipped ancestors (#36).
Acceptance: unit — a `HitTestDifferentialTest.cpp` in the 100 % gate over the upstream fixtures plus
`pointerEvents` and transform cases; golden — `hit-paint.png` unchanged; e2e — `hit-paint.json` unchanged.
DEDUPE: #35 (hit-versus-paint), #97/#98/#121 (animation hit testing), #64 (in review).

### E10. `test(harness): the upstream IntegrationTests bundles under markTestPassed`

Context: React Native's original headless integration bundles (listed in react-native-macos
`packages/rn-tester/IntegrationTests/`: `LayoutEventsTest`, `TimersTest`, `PromiseTest`, `AppEventsTest`,
`SyncMethodTest`, `WebSocketTest`, `ImageSnapshotTest`, ...) assert in JS and call `TestModule.markTestPassed`;
RNW's `Desktop.IntegrationTests/RNTesterHeadlessTests.cpp` runs the same protocol against its host. #214 plans
`MarkTestPassed` as a channel command but names no bundles for it. The platform-neutral five
(`LayoutEvents`, `Timers`, `Promise`, `AppEvents`, `SyncMethod`) are the first consumers.
Acceptance: e2e (headless, no compositor) — each bundle runs under `hello_react` with the `MarkTestPassed`
protocol and is green; `WebSocket` and `ImageSnapshot` are deferred to #79 and the golden rig respectively,
named in the PR.
DEDUPE: #214 (the protocol), #210 (an alternative host for the same bundles), #24 (React must boot first).

### E11. `test(animation): link upstream LayoutAnimation suites if #123 decides yes`

Context: `react/renderer/animations/tests/{LayoutAnimationTest,MutationComparatorTest}.cpp` (22 tests) exercise
`LayoutAnimationDriver` with `react/test_utils/{Entropy,MockClock,shadowTreeGeneration}.h` and the mounting
stubs — all vendored, none built. They are the only upstream suite whose value depends on a pending decision.
Acceptance: unit — if #123 decides to implement, the two files compile unmodified into `rnl_core_tests` with
`react/renderer/animations` added, in the same PR as the feature; if #123 refuses, the exclusion reason in CMake
links the decision and this issue closes.
DEDUPE: #123 (fold in as an acceptance line there if preferred), #142 (Reanimated layout animations, a
different mechanism).

## F. Order of attack

1. **Zero new code, this week.** E1: extend the two `file(GLOB ...)`/`target_sources` lists in
   `packages/core/tests/CMakeLists.txt` by the 24 files in section B's first table and fix the two wrong
   comments. Expected yield: roughly 250 upstream `TEST` macros, including `FindNodeAtPoint`,
   `LayoutableShadowNode`, `PointerEventsProcessor`, `StateReconciliation` and `TaskDispatchThread`, in the
   existing `unit` job. E4 (oracle over all 39 directories) is the same PR or the next one.
2. **One CMake decision, then more links.** E2 needs the Hermes-plus-googletest configure answered once; the
   JSI conformance suite and `RuntimeExecutorShutdownTest` are the prize, and #212/#76/#77/#171 are waiting on
   the same seams. E3 rides the same binary with gmock enabled when #79 needs it.
3. **Harness-only, no React needed.** E7 (fail on logged errors) and E8 (per-element crop) change
   `scripts/e2e/*` and the scenario schema; both are Vitest-covered and touch no fixture semantics.
4. **After React boots in this host (#24, #22).** E5 first — it is in-process and needs no compositor — then
   #213's `visitAllPages`, E6's twelve flows, E10's five bundles, #215's Pressability rungs and #217's jest
   inheritance. #210's Fantom-style runner is what makes the 185 `*-itest.js` files runnable and is the
   natural host for E2's binary if the CMake decision goes that way.
5. **Decision-gated.** E11 waits on #123; the redbox and featureflags suites stay excluded with their reasons.
