# React Native's own Android and iOS test suites, mined — 2026-09-08

- Research report, 2026-09-08. Corpus: **`facebook/react-native` at tag `v0.87.1`, commit
  `a59eff64fa907ed6e919fafe6cbd26d1d54c2de3`** — the same tag `scripts/vendor.lock.json` pins, cloned in full
  (shallow, blob-filtered) because the vendored tree is sparse and holds only `ReactCommon`,
  `ReactCxxPlatform`, one JNI directory, `src` and the Hermes SDK. Everything this pass reads —
  `ReactAndroid/src/test`, `React/Tests`, `packages/rn-tester`, `private/react-native-fantom`,
  `Libraries/**/__tests__`, `packages/virtualized-lists` — is outside those paths. Plus the upstream issue
  tracker over 27 search terms, and the react-native-windows and react-native-macos trackers as a desktop
  cross-check. Reaction (`+n`) and comment (`cN`) counts are the API values on that date.
- **Operational note for the next pass.** `facebook/react-native` now 301-redirects to `react/react-native`,
  and the GitHub **search** API returns HTTP 422 for `repo:facebook/react-native`. Queries must use
  `repo:react/react-native`. Issue numbers are unchanged and `facebook/react-native/issues/N` URLs still
  resolve, so links in this report and in the filed issues use the old form deliberately.
- **Where React Native's mobile suites do NOT overlap with us, stated first so the rest is readable.**
  - **There is no Android instrumentation tree at this tag.** `ReactAndroid/src/androidTest` does not exist; the
    source sets are `debug/`, `debugOptimized/`, `main/`, `test/`. Files named `*InstrumentationTest.kt` are
    Robolectric. So "Android instrumentation tests" as a category is empty and every Android finding below is
    JUnit + Robolectric.
  - **Everything the JVM mediates** — `InputType` bitmasks, `AccessibilityNodeInfoCompat`, `MotionEvent`
    construction, Fresco, `SoLoader`, the annotation-reflection prop-setter machinery (~46 assertions across
    seven files) that codegen replaces for us. Mechanism only.
  - **Everything UIKit mediates** — `UITraitCollection`, `UIFont` family names, `UIScene`, popover
    presentation, `dispatch_once`, the `RCTBridge` legacy arch. Where an *invariant* survives the mechanism it
    is recorded; where it does not, it is dropped.
  - **Soft keyboards and orientation.** A desktop window has no on-screen keyboard and no orientation. Every
    `KeyboardAvoidingView` issue, every `supportedInterfaceOrientations` path and every soft-keyboard inset
    event is out of scope as a mechanism. What transfers, and is recorded, is the *shape*: an inset-derived
    event that re-emits on size change and dedupes otherwise, and a container recomputing its own padding from
    an externally reported obscured rect.
  - **App stores.** `UIRequiresFullScreen`, Play Store macrobenchmark modules, TestFlight. Nothing.
- **What is ours, and is why this pass exists.** React Native's two shipping platforms are the only places
  where its promises are written down as assertions, and its **tablet and large-screen paths are the only
  places where those assertions meet a window that changes size**. An iPad in Split View is a resizable
  container; a Chromebook running an Android app is a resizable container; DeX and Android freeform are
  resizable containers. Every one of them produces a bug report that a Wayland toplevel reproduces on the first
  drag, and upstream sees each of them through a narrow aperture — two discrete sizes, twice a session. We are
  the continuous version. Section T is that argument with the receipts.
- **Dedupe.** Every candidate was grepped against the number, title and labels of all **315 issues** in this
  repository (`gh issue list --state all --limit 1000`), across 60 keyword sweeps; the bodies of #36, #42, #44,
  #45, #50, #51, #91, #93, #115, #119, #125, #209, #210, #213, #214, #215, #217, #218, #227, #232 were read in
  full. Checked against the three earlier passes — `gpui-zed-2026-09-06.md`, `electron-2026-09-07.md`,
  `tauri-2026-09-07.md` — and against `test-suite-parity-2026-09-05.md`, `react-native-core-issues.md`,
  `issue-mining-2026-09-05.md`. Six keyword classes returned **zero** hits and became the spine of this pass:
  `tablet`, `foldable`, `split screen`, `multi-window`, `viewport`, `IntersectionObserver`,
  `getBoundingClientRect`, `removeClippedSubviews`, `prepareForRecycle`, `numColumns`, `initialNumToRender`,
  `hitSlop`, `scrollbar`.
- **Result: 26 issues filed (#418–#443), 21 existing issues amended by comment, 18 candidates dropped.**

---

## A. What the corpus actually contains, counted

Before any verdict, the sizes — because three of the four surfaces are far smaller than their reputation and
two whole categories the brief asked for do not exist.

| Surface | Files | Assertions | Note |
| --- | --- | --- | --- |
| `ReactAndroid/src/test` (Robolectric JUnit) | 84 `*Test.kt` (99 source files incl. 13 test utilities with 0 tests) | **538 `@Test`** | verified by direct count, not by the agent's estimate |
| `ReactAndroid/src/androidTest` | — | — | **does not exist at this tag** |
| `ReactAndroid/src/main/jni/react/fabric/test` | 1 `.cpp` | 4 `TEST_F` | already GoogleTest; outside the vendored sparse paths |
| `packages/rn-tester/RNTesterUnitTests` | 30 | **176 XCTest** | |
| `packages/rn-tester/RNTesterIntegrationTests` | 3 | 3, of which **every `RCT_TEST` is commented out** | see D1 |
| `packages/react-native/React/Tests` | 4 | **29 XCTest** | Mounting 11, Text 18 |
| iOS-hosted XCTest outside `React/` | 4 | — | `runtime/iostests` (host/instance lifecycle), `nativemodule/core/iostests` |
| `private/react-native-fantom` | — | **185 `*-itest.js` tree-wide** | the live corpus |
| `Libraries/**` + `src/private/**` non-itest Jest | **31** | ~330 | 8 of the 31 are LogBox |
| `packages/virtualized-lists/Lists/__tests__` | 8 | **162** | a separate workspace package |
| `packages/rn-tester/.maestro` | 13 flows + 2 helpers | — | 5 flows are Android-only |

Four of the ten Android areas the brief named are **empty**: `ReactScrollView` and `ReactHorizontalScrollView`
(zero tests of any kind), keyboard and IME (three assertions, all in `RootViewTest.kt`, all about deriving
soft-keyboard events from window insets), `AccessibilityNodeInfo` population (zero — only action dispatch), and
hit-slop / `pointerEvents` (zero — `TouchTargetHelperTest.kt`'s seven cases are all zero-scale transforms).

Six of the iOS areas are empty. An exhaustive grep for `XCTestCase` finds **no test file** for
`RCTMountingManager`, `RCTSurface`, `RCTFabricSurface`, `RCTSurfacePresenter`, `RCTSurfaceTouchHandler`,
`RCTSurfacePointerHandler`, `RCTScrollViewComponentView`, `RCTTextInputComponentView`, `RCTConversions` or
`RCTSafeAreaViewComponentView`.

And the structural finding that resizes #217: **upstream has migrated nearly all `Libraries/**` Jest suites to
Fantom.** There is no longer a `Dimensions-test.js`, `PixelRatio-test.js`, `View-test.js`, `Text-test.js`,
`ScrollView-test.js`, `Image-test.js`, `TextInput-test.js` or `StyleSheet-test.js`. 185 itests against 31
surviving Jest files. Recorded as a comment on #217.

---

## T. Tablet and large-screen behaviour — the section this pass exists for

The brief asked for this explicitly and it is where the value concentrated, so it comes before the
per-candidate tables rather than after them.

### T1. The aperture problem

React Native has a viewport. It is 390 × 844 at device pixel ratio 3 — an iPhone 14 — and it is the default of
`Fantom.createRoot()` in `private/react-native-fantom/src/index.js`. Across the entire tree there are **110
explicit `viewportWidth` occurrences in exactly nine files**. Those nine are the whole of upstream's
size-sensitive testing:

`ScrollView-viewCulling-itest.js` (44), `FlatList-itest.js` (36, four cases with explicit viewports),
`IntersectionObserver-itest.js` (106) and its benchmark, `ReactNativeDocument-itest.js` (9), `Text-itest.js`
(35), `View-flexBasisFitContent-itest.js`, `View-yogaNodeOwnerAssertion-itest.js`, and Fantom's own
`Fantom-itest.js`.

Every one is `REUSABLE-AS-IS`. None runs at a size a desktop window ever has. Running them under a matrix —
`390×844@3` to prove we did not break upstream's cases, `1280×720@1`, `1920×1080@1`, `2560×1440@2`, and
`1920×1200@1.25` for the fractional entry that no React Native platform has ever run a test at — is a
configuration change, and it is the highest value-to-effort item in this report. **Filed as #425.**

The `VirtualizedList` suite makes the trap concrete: it asserts the render window with `toMatchSnapshot()`
against a hardcoded `{width: 10, height: 20}` viewport, chosen so the snapshot stays small. Those snapshots are
not portable across a matrix and must be re-expressed as arithmetic — rendered cell count equals
`ceil(viewport / itemHeight) × windowSize`, bounded by data length. That rule is written into #425, #426 and
#428 so it cannot be quietly inherited.

### T2. Upstream's own tablet evidence, grouped

**Dimension propagation.** [#33005](https://github.com/facebook/react-native/issues/33005) (closed, +5, c5)
is `useWindowDimensions` being undebounced, filed *explicitly for desktop window dragging* and closed without a
fix. [#29323](https://github.com/facebook/react-native/issues/29323) (closed, **+23, c23**) is stale values
from constants snapshotted at init. [#30371](https://github.com/facebook/react-native/issues/30371) (open,
c10) is window dimensions frozen at the full-screen value in Android split screen.
[#33074](https://github.com/facebook/react-native/issues/33074) (open, c4) is wrong sizes on Chromebooks that
never update as the window is resized — the closest existing analogue to us that exists.
[#16152](https://github.com/facebook/react-native/issues/16152) (closed, **+29, c20**) is the original 2017 ask
to make `window` mean "our container" rather than the physical screen; the design decision everything else
inherits. [#51450](https://github.com/facebook/react-native/issues/51450) (closed, c6) is `RCTKeyWindow()`
returning nil at startup so the frame observer is never installed.
[#50621](https://github.com/facebook/react-native/issues/50621) (closed, c11) is the notification carrying
frame, theme and content-size changes **ceasing to fire entirely in a minor release** — the argument for
asserting that the event fires, not only that the values are right.
[#36118](https://github.com/facebook/react-native/issues/36118) and
[#30648](https://github.com/facebook/react-native/issues/30648) are the change event arriving **only on focus
loss** for an iPad app on macOS, so the UI shows the old layout while the window is already the new size.
[#52952](https://github.com/facebook/react-native/issues/52952) is a foldable fold not re-firing the hook — a
fold is an abrupt large resize, which is a maximise. **Amended onto #50** with an eleven-point acceptance
addition; the pattern behind it filed as #436.

**Layout and reflow.** [#58294](https://github.com/facebook/react-native/issues/58294) — **open, filed six days
before this pass** — root-causes a stale Yoga `computedFlexBasis` cached across a layout-constraint change,
with a reproduction. [#23443](https://github.com/facebook/react-native/issues/23443) (open since 2019, +8, c25)
is a `View` not re-measuring when a descendant `Text` rewraps at a new width — the canonical reflow bug, seven
years open. [#31171](https://github.com/facebook/react-native/issues/31171) is inline views keeping their old
measured width. [#57690](https://github.com/facebook/react-native/issues/57690) (closed) is a **position-only**
layout update dropped for a view under a flattened wrapper — and a resize is overwhelmingly position-only
mutations. [#57800](https://github.com/facebook/react-native/issues/57800) (open) is a differ crash reproduced
through rotation because rotation forces mass re-layout. [#48527](https://github.com/facebook/react-native/issues/48527)
(open, c16) is `flexWrap` wrapping unexpectedly *at certain widths* — and a drag visits every width.
**Filed as #433**, whose central assertion is an A→B→A layout-idempotence property that subsumes most of the
list. [#29712](https://github.com/facebook/react-native/issues/29712) (open, **+73, c34**, the
highest-reaction measurement issue in the tracker) and [#54988](https://github.com/facebook/react-native/issues/54988)
(measure ignoring ancestor transforms — and a fractional output scale *is* a scale transform) **amended onto
#115**.

**The missing primitive.** [#51815](https://github.com/facebook/react-native/issues/51815) (open) —
*"The case for possibility of triggering synchronous layout from UI thread"* — is the one upstream issue that
names what `xdg_toplevel.configure` needs and does not have.
[#16060](https://github.com/facebook/react-native/issues/16060) (open, +33, c29) is the same hole from the
other side: iOS gets no "will change to size X" pre-notification, so an orientation change snaps instead of
interpolating. **Wayland gives us the pre-notification, with a serial, every time** — whether we can use it
depends entirely on which thread may run layout. [#3219](https://github.com/facebook/react-native/issues/3219)
(closed, c19, 2015) is the architectural thread: React Native never grew a first-class configuration-change
pipeline, which is why every bug in this section exists. **Filed as #434, `needs:decision`.**

**Resize must not remount.** [#25040](https://github.com/facebook/react-native/issues/25040) (closed, +6) and
[#19214](https://github.com/facebook/react-native/issues/19214) are the component tree being reconstructed on a
split-screen resize, losing state and scroll. At the ~1 ms per view
[#51869](https://github.com/facebook/react-native/issues/51869) (closed, c27) measured, a hundred-view remount
is a stall, not a slow frame. Android has an excuse — the Activity is recreated around a configuration change —
and a `wl_surface` is not, so for us it would be a choice. **Filed as #432**, together with scroll anchoring:
[#48325](https://github.com/facebook/react-native/issues/48325) (offset preserved in pixels rather than against
an anchor item, so after a width change it points at a different row),
[#47153](https://github.com/facebook/react-native/issues/47153) (closed, c31 — offsets resolved against stale
metrics), [#58186](https://github.com/facebook/react-native/issues/58186) (a one-frame jump in the anchoring
mechanism itself, **amended onto #292**).

**Density is per-surface.** [#57183](https://github.com/facebook/react-native/issues/57183) (open) is text
rendering roughly `density×` too large on a display of different density, because `DisplayMetricsHolder` is
initialised once and never updated — filed against DeX and freeform, Android's desktop mode.
[#55659](https://github.com/facebook/react-native/issues/55659) (closed) is the same singleton one layer down,
in `PixelUtil` itself, titled for desktop mode.
[#56894](https://github.com/facebook/react-native/issues/56894) (open) is `screen.scale` reporting the primary
display's density on a secondary one, so Fabric lays out larger than the window and the remainder is black.
[#58360](https://github.com/facebook/react-native/issues/58360) — **open, filed two days before this pass** —
is a display-size change breaking hit testing entirely, because touch targets are computed in the wrong
coordinate space. [#18096](https://github.com/facebook/react-native/issues/18096) is the threading half:
reading the screen scale synchronously off the main thread can **deadlock**, so it must be cached, and caching
it is what makes it stale. **Amended onto #51** with a five-point acceptance addition.

**Text under a changing width.** [#57512](https://github.com/facebook/react-native/issues/57512) (open) is a
runtime text-metric change leaving stale paragraph layout and `onTextLayout` reporting stale widths — for us,
an output-scale change that does not invalidate the paragraph cache.
[#52642](https://github.com/facebook/react-native/issues/52642) (open, **+17, c13**) and
[#56799](https://github.com/facebook/react-native/issues/56799) are `adjustsFontSizeToFit` choosing the wrong
size when the width constraint changes — and the width constraint changes continuously during a drag.
[#54422](https://github.com/facebook/react-native/issues/54422) is fractional metrics with inline views
producing inconsistent wrapping. [#58124](https://github.com/facebook/react-native/issues/58124) is
`maxFontSizeMultiplier` capping font size but not line height.
[#56961](https://github.com/facebook/react-native/issues/56961) (open, +9),
[#49459](https://github.com/facebook/react-native/issues/49459) (a font-scale listener that fires once and
dies) and [#45655](https://github.com/facebook/react-native/issues/45655) (open, c13 — a font-scale change not
re-laying-out mounted text) are the scale-change-observation failures. Cross-referenced onto #113, #252 and
#342 through #429, which owns the prior question: whether our headless runner measures text at all.

**Lists on a large screen.** [#50439](https://github.com/facebook/react-native/issues/50439) — `numColumns`
cannot change at runtime and the documented key workaround does not work — is a **hard blocker for responsive
grids**, which is the most ordinary large-screen layout there is and is literally what the flagship is. **Filed
as #438.** The `initialNumToRender` default of 10 fills a phone and a fifth of a 1440-pixel window, and the
expansion to a full viewport happens on a later tick — **filed as #439**, with a measurement first and a fix
chosen from the number.

**The transitions, not the end states.**
[react-native-windows#16251](https://github.com/microsoft/react-native-windows/issues/16251) (open) is a
scrollbar that never reappears after a resize takes content from non-scrollable back to scrollable, because
visibility is a one-way latch — a transition that exists only on desktop, where scrollability changes because
the *viewport* changed rather than because the content did.
[react-native-windows#12775](https://github.com/microsoft/react-native-windows/issues/12775) is a `TextInput`
caret vanishing after a resize. `RCTViewComponentViewTests.mm`'s four `removeClippedSubviews` cases are the
third instance: toggling clipping must remount all children **in their original index order**. iOS hits that
toggle at a discrete Split View snap; we hit it whenever a child crosses the viewport edge, which during a drag
is continuous and bidirectional. **Filed as #437.**

**The e2e gap.** There is **no resize, orientation, multi-window, keyboard-shortcut, hover or scroll-wheel flow
anywhere** in the 13-flow Maestro corpus, and there cannot be — Maestro has no window-resize primitive because
mobile has no resizable window. `scrollWheel` appears nowhere in the React Native tree either, because iOS
delivers trackpad scroll as a pan gesture, so `wl_pointer.axis` handling has no upstream precedent at all.
**Filed as #430** (resize drag as a continuous configure stream, window-state transitions, output-scale change,
multi-window focus) and **#431** (a golden key with a size, scale and decoration-state axis — upstream keys its
references on scale (`@2x`/`@3x`) and on platform, and a fractional 125 % scale fits neither).

### T3. What iPad multitasking actually is, in code

The brief asked for Split View, Slide Over and Stage Manager. **Grep returns zero hits for `SlideOver`,
`Slide Over`, `Stage Manager`, `StageManager` and `UIRequiresFullScreen`.** One hit for `SplitView`, in a doc
comment. React Native handles the whole of iPad multitasking in **one place, without naming it** —
`React/CoreModules/RCTDeviceInfo.mm`:

```objc
BOOL isRunningInFullScreen = window ? CGRectEqualToRect(window.frame, window.screen.bounds) : YES;
BOOL isResizingOrChangingToFullscreen = !isRunningInFullScreen || !_isFullscreen;
```

It infers "I am in a resizable container" by comparing rectangles, and then the guard degenerates to *always
emit*, because Apple provides no resize-began or resize-ended signal. **A Wayland toplevel is never
`frame == screen.bounds` except when fullscreened**, so a naive port lands permanently in that branch — and
Wayland delivers `maximized`, `fullscreen`, `tiled_*`, `activated` and `suspended` explicitly, which is
strictly better information. Recorded on #50, #218 and #430: use the states, do not reproduce the inference,
and add the coalescing layer iOS never had to write.

`UIUserInterfaceIdiomPad` has exactly **three** sites in the tree: popover anchoring in
`RCTActionSheetManager.mm` (anchored only on iPad; arrowless with no anchor — and for us an anchor is a
*protocol argument* of `xdg_popup`, so the iPad branch must be the only branch — recorded on **#262**), the
Modal orientation default in `RCTModalHostViewComponentView.mm`, and the `interfaceIdiom` constant in
`RCTPlatform.mm` (`phone`|`pad`|`tv`|`carplay`|`vision`|`mac`|`unknown`; the presence of `mac` proves the
enumeration is extensible and that a new platform lands consumers in `unknown` — **filed as #443**).

And the deliberate omission that is the best news in this report: **`UIUserInterfaceSizeClass`,
`horizontalSizeClass` and `verticalSizeClass` have zero occurrences in the entire tree.** React Native consumes
no size classes; it exposes raw point dimensions and lets JavaScript do the breakpoint arithmetic through
`useWindowDimensions`. So every React Native application in the wild already does continuous-breakpoint layout,
and a freely resizing window is *better* served than an iPad is — provided our dimensions are accurate and
timely. We should not invent a Linux size-class trait, and #443 carries a test whose name says why not.

The trait-collection lesson is the mirror image. `RCTViewComponentView.mm` guards on
`hasDifferentColorAppearanceComparedToTraitCollection:` before re-resolving colours, because iOS delivers one
coarse "a trait changed" callback and **a resize is a trait change**; a `TODO` in `RCTTextInputComponentView.mm`
records Apple later replacing this with `registerForTraitChanges`. Wayland already gives the signals
separately — `wl_output.scale`, `preferred_buffer_scale`, `xdg_toplevel.configure`, the portal's colour-scheme
and contrast keys. Do not funnel them into one notification. Recorded on **#51**.

Finally, `RCTKeyWindow()`. It is reached for in about ten independent iOS subsystems — dimensions, appearance,
dev menu, dev loading view, alerts, LogBox, the paused-in-debugger overlay, frame timings, the status bar, and
`RCTHost`. Every one is a "which window?" answered by a global. react-native-macos'
[#2296](https://github.com/microsoft/react-native-macos/issues/2296) (dimensions `{0,0}` whenever no window has
focus) and [#2129](https://github.com/microsoft/react-native-macos/issues/2129) (a popover-hosted surface
reporting nothing) are that pattern meeting a real desktop, and
[react-native-windows#4050](https://github.com/microsoft/react-native-windows/issues/4050) has been open since
2020 asking upstream to fix the model. On Wayland **focus and geometry are fully independent** — a surface can
be sized, visible and unfocused indefinitely — so coupling them produces #2296 by construction. **Filed as
#436, `needs:decision`:** the surface is an explicit parameter of every window-scoped API, or it is not, and
deciding now costs a signature while deciding later costs a refactor of every module. Upstream's own nascent
answer is `ExtraWindowEventListenerTest.kt` (8 assertions on create/destroy listener registration, fan-out,
idempotent add and removal isolation), which is the contract to match rather than invent.

---

## B. Reusable as-is — tests that port

Verdict `REUSABLE-AS-IS` means the assertions mention no platform type and the port is a translation.

| Upstream | Assertions | Filed |
| --- | --- | --- |
| `PixelUtilTest.kt`, `MatrixMathHelperTest.kt` (29), `SkewMatrixHelperTest.kt` (17), `BorderRadiusStyleTest.kt`, `ColorStopTest.kt`, `DimensionPropConverterTest.kt`, `TextDecorationStyleTest.kt`, `TextTransformTest.kt`, `TextAttributePropsTest.kt`, `ColorUtilTest.kt`, `TextLayoutManagerAbsoluteLayoutWithFractionalPixelTest.kt`; iOS `RCTImageUtilTests.m`, `RCTConvert_YGValueTests.m`, `RCTAnimationUtilsTests.m` | **99** | **#418** |
| `SurfaceMountingManagerTest.kt` (9), `SurfaceMountingManagerEventOrderingTest.kt` (15), `SurfaceMountingManagerSynchronousMountPropsTest.kt` (9), `FabricMountingManagerInstrumentationTest.kt` (8), `FabricMountingManagerTest.cpp` (4), `RCTComponentViewRegistryTests.mm` (3) | **48** | **#419** |
| `RCTComponentViewRegistryTests.mm` + `RCTViewComponentViewTests.mm` (8) + the **17 untested `prepareForRecycle` implementations** + `TouchTargetHelperTest.kt` (7) + `ReactViewGroupTest.kt` (2) | 20 ported, 17 generated | **#420** |
| `EventTargetDispatching-itest.js` (42), `EventDispatching-itest.js` (14), `ResponderEventTarget-itest.js` (24), `dom/events/__tests__/` (52) | **132** | **#421** |
| `IntersectionObserver-itest.js` (106), `MutationObserver-itest.js` (23), `ReactNativeElement-itest.js` (45) | **174** | **#422** |
| `setup/__tests__/` (73), `abort-api` (40), `structuredClone` (32), `performance` (60), `oldstylecollections` (19), `geometry` (7), `errors` (4), `timers` (10), `featureflags` (7) | **~250** | **#423** |
| `ScrollView-viewCulling-itest.js` (44), `ScrollView-itest.js` (8), `virtualview` (12) | **64** | **#426** |
| `accessibilityPropsSuite.js`, `commonPropsSuite.js`, `BaseViewManagerTest.kt` | shared, + 10 | **#427** |
| `packages/virtualized-lists/Lists/__tests__/` — 8 files incl. `VirtualizedList-test.js` (78), `ListMetricsAggregator-test.js`, `ViewabilityHelper-test.js` (13) | **162** | **#428** |
| `ReactAccessibilityDelegateTest.kt` (13) + `BaseViewManagerTest.kt` a11y cases + `RCTParagraphComponentViewTests.mm` (5) | **~23** | **#442** |
| `RootShadowNodeTest.cpp`, `StateReconciliationTest.cpp`, `LazyShadowTreeRevisionConsistencyManagerTest.cpp`, the `mounting/tests` differ suites, `PointerEventsProcessorTest.cpp` | — | **amended onto #227** |

`RootShadowNodeTest.cloneWithLayoutConstraints` deserves its own line: it asserts that cloning the root with new
`LayoutConstraints` **dirties it and forces a relayout**. That single test is the `xdg_toplevel.configure` path
stated as an assertion, and it sits on #227's list as one low-priority item among two dozen. The comment on
#227 promotes it.

## C. Reusable as an invariant — behaviour we must match

| Upstream | The invariant | Where it went |
| --- | --- | --- |
| `DeviceInfoModuleTest.kt` (7) | dedupe by value, re-arm after emit, window and screen as distinct keys, insets subtracted or not by a stated policy | **#50 amended** |
| `DisplayMetricsHolderTest.kt` (9) | reading before init **throws**; init is idempotent; `scaledDensity` survives; a missing window service falls back rather than throwing | **#50 amended** |
| `RCTDeviceInfo.mm` | five inputs, one `isEqual:` gate, suppression while inactive, **state advances only when publishing**, never `dispatch_once` a geometry fact | **#50 amended** |
| `Dimensions.js` | subscribe **before** reading constants; divide physical pixels by scale **in JS** to keep fractional precision; the first `set()` fires no change | **#50 amended** |
| `RCTSafeAreaViewComponentView.mm` | round to pixels, then compare against a **scale-derived** epsilon `1/scale + 0.01`; `nullptr` means no commit | **#374, #435** |
| `SurfaceMountingManagerSynchronousMountPropsTest.kt` (9) | the render-thread animation prop store wins over a stale commit | **#419** |
| `ReactSurfaceTest.kt` (9) | surface lifecycle; a second attach throws; `getLayoutSpecs` is where size enters | **#218 amended** |
| `ExtraWindowEventListenerTest.kt` (8) | multi-window listener registration, fan-out, idempotent add, removal isolation | **#218, #436** |
| `RCTSurfacePointerHandler.mm` | pointer type from device class; a real pointer reports its button mask, a finger is hardcoded to button 1; two hover recognizers | **#36 amended** |
| `RCTSurfaceTouchHandler.mm` | identifiers are returned on **cancel**, not only on a clean end — and `wl_pointer.leave` mid-drag is a cancel | **#247 amended** |
| `RCTSurfaceSizeMeasureMode.mm` | exact versus at-most per axis — which is `xdg_toplevel` tiled/maximised versus floating | **#434** |
| `RCTFabricModalHostViewController.mm` | an unsatisfiable constraint falls back to unconstrained with a log, never deadlocks | **#262 amended** |
| `RCTActionSheetManager.mm` | the iPad anchor branch is the only branch; `xdg_popup` requires an anchor rect | **#262 amended** |
| `CustomLineHeightSpanTest.kt` (4), `ReactTextViewTest.kt` (2), `RCTParagraphComponentViewTests.mm` | tight `lineHeight` must not clip font bounds; ink outside the line box is not clipped under `overflow: visible`; a link wrapping two lines is **one** accessible element | **#429, #266 amended** |
| `ReactTextInputPropertyTest.kt` (23) | incremental prop updates must not reset unrelated state; identical text from JS causes no replacement, therefore no cursor jump | dropped as a table, kept as two invariants — see D6 |
| `RootViewTest.kt` (3) | an inset-derived event re-emits on size change and dedupes otherwise | **#374 amended** |
| `RCTTestModule.mm` / `RCTTestRunner.m` | the whole JS↔native test protocol is four methods; `recordMode` is a mode of the same runner; `expectErrorRegex` is part of the protocol | **#214 amended** |
| `image.yml` | an in-app runner whose results the driver reads as rendered text | **#214 amended** |
| Maestro flow shape | `extendedWaitUntil` against cold start; `when: platform:` degenerating to an empty **passing** flow; scale- and platform-keyed goldens | **#232, #431** |

## D. Dropped, with reasons

1. **`RNTesterIntegrationTests` (3 files, 14 JS drivers).** Every `RCT_TEST` line is commented out. The
   in-source reasons are "based on deprecated RCTBridge", "flakiness #8686784", "never passed", and a
   `TODO(T225745315)` where `AccessibilityManager` is nil so `RCTDeviceInfo` falls back to `fontScale = 1.0`
   and the assertion fails. **Not a corpus; a warning**, and #236 should know it before planning to run those
   bundles. The lesson — do not hang the integration rig off the application shell — is recorded on #214. The
   one JS file with transferable content, `LayoutEventsTest.js` (`onLayout` fires after mount and again after a
   size change, and `measure()` agrees with the reported rect), is subsumed by #435 and #115.
2. **`InteropUiBlockListenerTest.kt` (10), `FabricUIManagerTest.kt` (2), the seven prop-annotation and
   constants files (~46).** Old-architecture interop and Java annotation reflection. New-Arch-only by ADR-0001;
   codegen replaces the mechanism. The four mount-phase names (`willMountItems`, `willDispatchViewUpdates`,
   `didMountItems`, `didDispatchMountItems`) are kept as a checklist in #419 and nothing else.
3. **`RCTAllocationTests.m`, `RCTPerformanceLoggerTests.m`.** Legacy bridge teardown; and the latter has no
   `test*` methods left at all.
4. **The Android networking, blob, dialog, share, intent, camera and Fresco module suites (~140).** Platform
   module implementations with no shared contract. Two exceptions were kept as invariants and folded into
   existing issues rather than filed: `TimingModuleTest.kt` (10 — timer and idle-callback scheduling relative
   to frame callbacks, which matters because our scheduler is driven by `wl_surface.frame`; belongs to #171 and
   #423) and `AppStateModuleTest.kt` (5 — active/background transitions, whose desktop analogue is focus and
   occlusion; belongs to #218's `suspended` clause).
5. **`androidx.benchmark` macrobenchmark module.** On-device startup measurement. The hermes/jsc flavour split
   is a reasonable model for a perf CI matrix and is noted here only.
6. **`ReactTextInputPropertyTest.kt` as a port (23).** The `InputType` bitmask algebra is JVM-specific and the
   keyboardType-to-IME-hint table has no desktop meaning. Two invariants inside it are real and were kept:
   incremental prop updates must not reset unrelated state, and identical text arriving from JS must cause no
   replacement (hence no cursor jump). Both belong to #17, #53 and #340, all of which already exist; filing a
   third issue for them would have been a duplicate.
7. **`TouchEventDispatchTest.kt` (3, ~600 lines of golden fixtures).** The payload construction is
   `MotionEvent`-bound. The *pattern* — an ordered event-sequence golden as a checked-in fixture — is already
   what our e2e traces are.
8. **`RCTNativeAnimatedNodesManagerTests.m` (22), `NativeAnimatedNodeTraversalTest.kt` (27),
   `NativeAnimatedInterpolationTest.kt` (9), `AnimatedNative-test.js` (40).** The animated graph is mirrored in
   C++ at `ReactCommon/react/renderer/animated/tests`, which #132 already links as the drift oracle. Filing
   ports of the JVM and ObjC mirrors would duplicate it.
9. **`RCTMethodArgumentTests.m` (15), `RCTModuleMethodTests.mm` (8), `RCTTurboModuleArrayBufferTests.mm` (5).**
   TurboModule argument coercion — owned by #21 and #85 (codegen determinism).
10. **`RCTFontTests.m` (10).** Weight and variant resolution logic is reusable in principle; `UIFont` family
    names are not, and #372 already owns resolving the default family from the desktop's font configuration.
11. **`RCTConvert_UIColorTests.m` (6).** Semantic colour resolution under a trait collection — #52 owns
    `PlatformColor`. Its mechanism of setting a *global current trait collection* inside a test is a good model
    for a settable global appearance in our tests and is noted on #51.
12. **`KeyboardAvoidingView` (5 issues, +147/+61/+60/+31/+19).** [#16826](https://github.com/facebook/react-native/issues/16826)
    is the highest-signal issue in the entire survey and it is a soft-keyboard mechanism we do not have. Kept
    only as the *shape* — a container recomputing padding from an externally reported obscured rect, with a
    guard for a bogus zero rect mid-transition — recorded on #374. Applications will ship
    `KeyboardAvoidingView` and it must be a harmless no-op; that belongs to the ecosystem matrix (#87), not to
    a renderer issue.
13. **`Keyboard-test.js` (7).** Soft-keyboard show and hide. API-shape smoke test at most.
14. **Orientation as a subject.** `supportedInterfaceOrientations`, `UIDeviceOrientationDidChange`,
    `RCTConvert`'s orientation-mask table, and [#25](https://github.com/facebook/react-native/issues/25)
    (closed, +21, c67, issue number 25 — day one). A desktop window has no orientation. Every orientation issue
    was mined for its *resize* content and then dropped as a mechanism; the one exception kept is
    [#29290](https://github.com/facebook/react-native/issues/29290) (open, +8, c45 — width and height delivered
    **transposed** mid-transition), whose invariant is that a dimension pair must be delivered atomically, and
    that is in #50's amendment.
15. **`RCTIsIPhoneNotched` and the safe-area family.** React Native core has no safe-area story at all — it
    lives in `react-native-safe-area-context`. `SafeAreaView-itest.js` has exactly **one** case, "renders with
    children", and asserts no inset behaviour. Our equivalent question (client-side decorations, tiling gaps,
    panel struts) has no upstream precedent; #374 owns it and there is nothing to inherit.
16. **`Animated.event` from `onLayout` with the native driver**
    ([#43430](https://github.com/facebook/react-native/issues/43430), open, c2 — you cannot drive a native
    animation from a layout change, so resize-driven animation must cross to JS every frame). Real, and small;
    #19 and #143 own the native driver and the services Reanimated expects. Recorded here rather than filed to
    keep the batch at 26.
17. **`testutils/shadows/` (13 files, 0 tests).** Robolectric shadows faking the JNI boundary so "native" map
    types work in-process. The idea is what our C++ harness gets for free by having no boundary.
18. **`private/helloworld/ios/HelloWorldTests`.** A template smoke test.

## E. The issues filed

| # | Title | Epic | Priority |
| --- | --- | --- | --- |
| 418 | `test(core)` platform arithmetic — pixel and font-scale conversion, matrix decomposition, border-radius priority, gradient stops | 209 | P2 |
| 419 | `test(core)` mount-item idempotence, per-tag event queueing, the synchronous-props override | 209 | P1 |
| 420 | `test(core)` the component-view pool is strictly balanced; recycle resets are generated | 209 | P1 |
| 421 | `test(input)` the `renderer/core` event-dispatching itests as the Wayland input conformance gate | 209 | P1 |
| 422 | `test(core)` IntersectionObserver and MutationObserver as the headless geometry gate and leak canary | 209 | P2 |
| 423 | `test(core)` the runtime-conformance globals gate | 209 | P2 |
| 424 | `test(harness)` the flag-pragma matrix, the hand-rolled expect runtime, the `llvm-cov` path | 209 | P2 |
| **425** | **`test(harness)` the viewport matrix — nine files that have never run at a desktop size** | 209 | **P1** |
| 426 | `test(renderer)` ScrollView view culling at desktop viewport sizes | 209 | P1 |
| 427 | `test(core)` the shared accessibility and common prop suites, run against every component | 209 | P2 |
| 428 | `test(harness)` the `virtualized-lists` package's own 162 assertions | 209 | P2 |
| 429 | `test(text)` upstream's headless runner measures no text — decide whether ours does | 209 | P1, `needs:decision` |
| **430** | **`test(harness)` the e2e driver has no resize, window-state or output-scale primitive** | 209 | **P1** |
| 431 | `test(harness)` a golden gains a size, scale and decoration-state key | 209 | P2 |
| 432 | `fix(core)` a resize relayouts, it does not remount — and scroll anchors to an item | 173 | P1 |
| 433 | `fix(renderer)` A→B→A must give back A — Yoga caches that survive a constraint change | 173 | P1 |
| **434** | **`feat(core)` `configure` needs a layout before the ack, and Fabric has no synchronous layout** | 173 | **P1, `needs:decision`** |
| 435 | `fix(renderer)` `onLayout` is gated on value equality at the current output scale | 173 | P1 |
| **436** | **`feat(core)` the surface is an explicit parameter of every window-scoped API** | 173 | **P1, `needs:decision`** |
| 437 | `test(renderer)` the resize transitions, not the end states | 173 | P2 |
| 438 | `feat(renderer)` `numColumns` cannot change at runtime | 173 | P2 |
| 439 | `fix(renderer)` `initialNumToRender` is ten — measure the blank band | 173 | P2 |
| 440 | `feat(input)` an application-level keyboard-shortcut registry | 173 | P2 |
| 441 | `feat(input)` keyboard scrolling and its arbitration with a focused field | **1** | P2 |
| 442 | `test(a11y)` action dispatch and locale-invariant role parsing | 177 | P2 |
| 443 | `feat(modules)` the platform constants are an ecosystem API | 175 | P2, `needs:decision` |

All carry `origin:rn-core`, a `kind:*`, an `area:*` and `status:ready`; the four `needs:decision` issues carry
it alongside `status:ready` because each has acceptance criteria and no open dependency — the decision *is* the
work. `platform-parity:android` (5), `platform-parity:ios` (4), `platform-parity:ipad` (4),
`platform-parity:macos` (1), `platform-parity:windows` (1), `platform-parity:host-linux` (4).

**#441 is parented to #1, not to #173, because #173 has reached GitHub's hard cap of 100 sub-issues** and
`addSubIssue` now refuses further children. Recorded as a comment on #173 so the next agent filing into M1 sees
it before hitting it. Re-parent when there is room.

## F. Existing issues amended

Twenty-one, each with an upstream pointer and, where warranted, a written acceptance addition: **#36** (there is
no upstream oracle for the hover chain — one Android assertion, zero on iOS — so the oracle is the W3C
specification, plus the pointer-type and button-mask model), **#42** (three costs the paint count does not
include), **#44** (no fractional asset suffix exists anywhere; macOS never solved resize-time image scaling),
**#50** (eleven-point acceptance addition — the largest amendment in this pass), **#51** (five-point addition;
density is per-surface and the trait signals must stay separate), **#91** (its only oracle is in an unclaimed
package; two implied capabilities filed), **#115** (a 45-assertion oracle, plus the +73 flattening bug),
**#119** (the RTL no-swap mode and vertical-RTL), **#214** (upstream's channel is four methods; the in-app
result-text pattern), **#215** (Pressability now exists at two layers and the layer below has 80 untested
assertions), **#217** (the 185-versus-31 split resizes this issue in both directions), **#218** (Wayland's
states beat iOS's rect inference; the surface lifecycle machine), **#227** (`RootShadowNodeTest` is the resize
oracle and belongs in the first tranche), **#231** (snapshots need a size axis), **#232** (thirteen flows, not
twelve, five Android-only, and four patterns worth adopting), **#247** (a shipped upstream crash, and
`wl_pointer.leave` mid-drag is a cancel), **#262** (an anchor is a protocol argument, not an iPad option),
**#266** (a wrapping link is one accessible element, and wrapping is a function of width), **#267** (the
13-assertion unit oracle beneath the AT-SPI mapping), **#292** (the anchoring mechanism's own one-frame jump),
**#374** (the scale-derived epsilon, and Android's inset arithmetic asserted with exact numbers).

## G. What this pass was unsure about

- **The `needs:decision` label alongside `status:ready`.** Four issues (#429, #434, #436, #443) are decisions
  with acceptance criteria and no open dependency. The Multi-Agent Protocol's table defines `status:ready` as
  "groomed, unblocked, free to take", which they are — the deliverable is a recorded decision plus its tests.
  If the house style is that `needs:decision` implies `status:blocked`, four labels need flipping and the
  blocker named in a comment.
- **#441's parent.** Parenting M1 work to #1 because #173 is full is a workaround, not a decision. The real
  choice is whether M1 gets a second epic, and that is above this pass.
- **Whether #438 (`numColumns`) is ours at all.** The fix may belong in the JavaScript list implementation
  rather than in the platform, in which case the issue closes on a written statement and an ecosystem note. The
  acceptance says so explicitly rather than assuming.
- **The viewport matrix's five entries.** Chosen for coverage of integer, high-integer and fractional scales;
  the runtime cost is not known until #425 runs. The acceptance makes the total runtime a tracked number and
  permits reducing entries with a stated reason, rather than pretending five is derived.
- **Two counts were not independently re-verified**: the 176 XCTest methods in `RNTesterUnitTests` and the
  ~250 platform-seam-free itest assertions in #423's list are agent-reported sums over per-file counts. The
  Android totals (84 files, 538 `@Test`) and the per-file counts in #418, #419, #420 and #442 were re-counted
  directly against the clone.
- **No collision check was possible against work in flight.** The tracker was read immediately before filing
  (315 issues, highest number #414) and no other agent held `status:in-progress` on anything this pass touches,
  but the Zed, Electron and Tauri passes each discovered a same-minute collision after the fact. If another
  research pass files into #173 or #209 today, #425 and #430 are the two most likely duplicates, because they
  are the two most obvious gaps.
