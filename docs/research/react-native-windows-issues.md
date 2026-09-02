# What `react-native-windows` issues teach `react-native-linux`

- Source: `microsoft/react-native-windows`, issue tracker read-only via the GitHub search API on 2026-09-02.
- Corpus: **5,349 issues** total (`is:issue`, excludes PRs), **720 open**. An order of magnitude larger than
  `react-native-macos`'s 663 — this is the React Native fork that reached institutional scale, and its label
  taxonomy is the artifact of that scale (see *Label taxonomy*, below).
- The machine-readable plan this document explains is `scripts/issue-plans/windows.json`: **30 proposed issues**,
  each with milestone, labels, priority, and a body citing RNW evidence. Across the 30 bodies, **155 distinct
  RNW issue numbers** are cited as evidence — every number in every table below traces to one of those bodies;
  none was invented for this write-up. Group counts below (`RNW cited`) count the citations per group, not a
  fresh search of the tracker.
- `windows.json`'s label set is the same 32-label scheme as `scripts/issue-plans/macos.json`, plus three additions
  the macOS batch did not need: `kind:regression`, `origin:flagship` (reserved for defects the flagship app finds
  by being played, not by research — unused by this batch, forward-declared for the next one), and
  `platform-parity:host-linux` (used three times below, where the Linux desktop convention itself is the oracle
  and no other React Native platform is).

## Why this repository is the second ancestor

`react-native-macos` answered "what breaks when React Native meets a desktop widget toolkit". `react-native-windows`
answers a different question: "what breaks when an out-of-tree fork survives ten years, three UI framework
generations (UWP XAML, WinUI 3, and a New Architecture migration), and enough contributors that process itself
becomes a defect surface." Two things make it worth a second, separate study rather than folding into the macOS
document:

1. **Scale exposes different bugs.** At 5,349 issues, single-report oddities average out and *load-bearing
   patterns* survive: independent lifetimes with no threading contract, a fork that must reproduce every implicit
   convention upstream gives away for free, and a style surface that keeps growing (CSS convergence: `boxShadow`,
   `filter`, `mixBlendMode`) faster than any platform ships it. Those patterns recur across dozens of RNW issues
   each, which is a different kind of evidence than a single well-reproduced macOS bug.
2. **RNW's label taxonomy is the opposite failure mode from macOS's.** Where macOS has one area label total,
   RNW has roughly ninety `Area:` labels, a `Workstream:` axis, a `Partner:` axis, a `Platform:` axis, and a
   `Parity:` axis — five overlapping dimensions applied inconsistently enough that some issues carry three area
   labels and others carry none. That is itself the lesson: more labels did not buy RNW better triage; it bought
   a taxonomy nobody could query with confidence. Our five-dimension scheme (`area:`/`kind:`/`priority:`/
   `platform-parity:`/`origin:`/`needs:`) is a deliberate reaction to both extremes.

Two caveats, stated so the mapping stays honest:

1. **They inherit WinRT/XAML; we inherit nothing.** A large slice of the tracker — `Area: WinUI`, `Platform: UWP`,
   `Platform: Xbox`, `Platform: WinAppSDK`, `Area: C#/C++ interop`, `.NET`/`.NET Archive` — is host-framework
   plumbing with no Linux analogue, excluded rather than stretched. `react-native-windows` also targets four
   *different* host surfaces (Desktop Win32, UWP, WinAppSDK, Xbox) from one codebase; we target exactly one
   (Wayland), which removes a whole axis of their fragmentation but does not remove the underlying lesson that an
   out-of-tree platform's implicit conventions must be reproduced deliberately, not discovered by users.
2. **Their New Architecture migration is further along and better documented than macOS's.** Several groups below
   (prop coverage, deforking, animated-value conformance) exist specifically because RNW's engineers wrote down
   the *process* fix after being burned, not just the bug fix. Those process artifacts — a coverage matrix, a
   validate gate, a determinism check — are the most portable output of this whole study, because we can adopt
   them before we accumulate the bug count that motivated them.

---

## Group 1 — Event payload identity: pointerEvents, key vs. code, mouse buttons

| RNW issue | Gist |
| --- | --- |
| [#8496](https://github.com/microsoft/react-native-windows/issues/8496) | Missing behaviours for the `pointerEvents` prop |
| [#8531](https://github.com/microsoft/react-native-windows/issues/8531) | A view with `borderRadius` ignores `pointerEvents="none"` — hit path used different geometry than paint the moment corners became round |
| [#10493](https://github.com/microsoft/react-native-windows/issues/10493) | Changing `pointerEvents` from `none` back to `box-none` at runtime never restored hit testing — applied once at mount, never invalidated |
| [#9767](https://github.com/microsoft/react-native-windows/issues/9767) | Applying `borderRadius` broke `zIndex` |
| [#16316](https://github.com/microsoft/react-native-windows/issues/16316) | A core `Switch` renders but never fires `onValueChange` on a mouse click |
| [#11049](https://github.com/microsoft/react-native-windows/issues/11049) | `keyUpEvents`/`keyDownEvents` check `code`, not `key` — labelled `Breaking Change`, still open |
| [#5821](https://github.com/microsoft/react-native-windows/issues/5821) | `isComposing` missing from `onKeyDown`/`onKeyUp` — a handler cannot tell a real Enter from an IME-commit Enter |
| [#16088](https://github.com/microsoft/react-native-windows/issues/16088) | Crash overwriting selected text in `<TextInput>` with a Japanese IME |
| [#6410](https://github.com/microsoft/react-native-windows/issues/6410) | Add a `button` number property to mouse events — open with reactions since 2020 |
| [#8275](https://github.com/microsoft/react-native-windows/issues/8275) | "Missing Mechanism to Expose Right Click" |

**Cause pattern.** React Native's cross-platform event contract underspecifies identity fields (`pointerEvents`
values, `key` vs. `code`, `button`/`buttons`), so each platform invents its own answer, and the invented answer is
wrong exactly where geometry, IME, or a non-primary input source complicates the simple case.

**Applies fully.** `pointerEvents` reaching `PointerEventsProcessor` at all is unverified today; a rounded hit
region matching the painted `SkRRect` rather than the layout rect is the same two-pipeline bug Group 1 of the
macOS study describes. `key`/`code`/`isComposing` map directly onto our `{key, ctrlKey, shiftKey, altKey, metaKey}`
placeholder payload and the unimplemented `xkb_compose_state`. `button`/`buttons` map onto `wl_pointer.button`
codes we already receive but do not yet expose.

**Our coverage.** #18 (pointer + keyboard pipeline, closed), #13 (view props, closed), #26 (IME, closed), #17
(`<TextInput>`, open) built the pipes; none asserts the identity fields survive rounded corners, runtime prop
changes, non-US keyboard layouts, or chorded mouse buttons. **Gap: three proposed issues** (`pointerEvents`
semantics, keyboard event identity, mouse button identity — all M1, P1/P1/P2).

---

## Group 2 — CSS-shaped visual props arrive faster than a widget toolkit can carry them

| RNW issue | Gist |
| --- | --- |
| [#2800](https://github.com/microsoft/react-native-windows/issues/2800) | "Drop Shadow support for View, Text, Image, and more" — open since 2019, 6 reactions, one of the most-reacted open issues in the repo |
| [#2796](https://github.com/microsoft/react-native-windows/issues/2796) | "Document View — Style API completion" |
| [#15352](https://github.com/microsoft/react-native-windows/issues/15352) | "Props Parity of Fabric as per Paper" |

**Cause pattern.** React Native's style surface keeps converging on CSS (0.76 added `boxShadow` and `filter`,
0.77 added `mixBlendMode`, `outline`, `boxSizing`). On a widget-tree platform each addition is a native-control
workaround that may never land — `#2800` is seven years old and still open. On a canvas it is a filter chain.

**Applies fully, and is the argument ADR-0001 already makes for choosing the canvas.** `docs/cpp-toolchain.md`
lists `shadowColor`/elevation, `boxShadow`, `filter`, `mixBlendMode`, `isolation`, `outline*` as explicitly not
implemented, with "each needs its own issue under M1" already written down.

**Our coverage.** #13 (view props, closed), #34 (gradients, open), #12 (damage tracking, closed) give the scene
graph and the damage model; none of them owns shadow, filter, blend, or outline drawing. **Gap: two proposed
issues** (`boxShadow`, and `filter`/`mixBlendMode`/`isolation`/`outline` — both M1, P1/P2).

---

## Group 3 — Prop coverage as a tracked matrix, not a backlog of one-off bugs

| RNW issue | Gist |
| --- | --- |
| [#6227](https://github.com/microsoft/react-native-windows/issues/6227) | "Master task: Add tests for every ViewManager, and test setting every prop/event" — still open |
| [#13183](https://github.com/microsoft/react-native-windows/issues/13183) | "Create issues to track remaining host component properties that we need to implement for fabric" |
| [#15352](https://github.com/microsoft/react-native-windows/issues/15352) | "Props Parity of Fabric as per Paper" — origin of the `Parity: Fabric vs. Paper` / `Parity: React Native` label pair |
| [#11152](https://github.com/microsoft/react-native-windows/issues/11152) | "ScrollView component parity for Fabric" — the per-component version of the same task |
| [#4037](https://github.com/microsoft/react-native-windows/issues/4037) | "Build visual tree compare tests that iterate over RNTester pages" — their attempt at the automated form |

**Cause pattern.** This is the single most transferable *process* lesson in the corpus. RNW filed prop bugs
one at a time until `Area: Fabric` alone reached roughly 570 issues, then created two `Parity:` labels because
prose could no longer describe how much prop drift existed. A machine-readable coverage matrix — implemented /
deliberately deviating / not implemented, one row per prop per component — would have made every one of those
issues a single generated report instead of hundreds of manually filed bugs.

**Applies fully, and is the highest-leverage single item in this batch.** We can have the matrix from the first
component, generated from Fabric's own component descriptors, rather than discover we need one at issue #570.

**Our coverage.** #13, #14, #15, #16, #17 (the per-component feature issues, mixed open/closed) and #6 (golden
rig, closed) built the components and the test rig; none of them produces a coverage report. **Gap: one proposed
issue**, deliberately P0 (`test(renderer): prop-coverage conformance`, M1).

---

## Group 4 — Text: font resolution, subpixel compositing, and RTL as a boot-time constant

| RNW issue | Gist |
| --- | --- |
| [#3463](https://github.com/microsoft/react-native-windows/issues/3463) | "Custom Fonts do not load like on other platforms" — open, 13 comments, since 2019 |
| [#16308](https://github.com/microsoft/react-native-windows/issues/16308) | Registered icon-font TTFs render **blank glyphs** until table checksums are recomputed — silent failure in the DWrite font path |
| [#16340](https://github.com/microsoft/react-native-windows/issues/16340) | Fabric text drawn with ClearType onto transparent composition surfaces fringes thin glyphs — the newest text bug in the tracker, and a compositing-model bug, not a control-integration bug |
| [#7070](https://github.com/microsoft/react-native-windows/issues/7070) | Switching LTR→RTL requires reloading the JS bundle; layout does not persist across restarts — open, 10 comments |
| [#7792](https://github.com/microsoft/react-native-windows/issues/7792) | The last character in wrapped RTL text fails hit testing — the geometry-disagreement bug again, where visual and logical order differ |
| [#4167](https://github.com/microsoft/react-native-windows/issues/4167) | `left`/`right` style properties mishandled in RTL |

**Cause pattern.** Two independent failure modes share a group because both are compositing-layer bugs rather
than control-integration bugs, which is unusual for text: a font that resolves silently to the wrong glyph, and
a rasterizer that assumes it owns the pixels underneath when the surface has an alpha channel. RTL adds a third:
platforms treat writing direction as a boot-time constant because nothing forces it to be dynamic.

**Applies fully, and #16340 is the most directly transferable text bug found in either study** — it is not an
integration bug with `NSTextView`/DWrite, it is a statement about `SkSurfaceProps` pixel geometry and gamma on an
alpha-composited Wayland surface, which is exactly our configuration. `docs/cpp-toolchain.md` already notes the
font pin exists "for exactly this class of reason," and separately states RTL is hardcoded left-to-right with no
`writingDirection` support and no golden.

**Our coverage.** #14 (text pipeline, closed), #11 (Yoga layout, closed), #6/#33 (golden rigs, closed), #17
(`<TextInput>`, open). **Gap: three proposed issues** (font asset registration, glyph rasterization policy, RTL —
all M1, P1).

---

## Group 5 — Numeric hygiene at the layout boundary

| RNW issue | Gist |
| --- | --- |
| [#8318](https://github.com/microsoft/react-native-windows/issues/8318) | `folly::toJson`: a NaN or Inf produced in layout survives serialization to JS and crashes there — 14 comments, `Area: Layout` |
| [#10197](https://github.com/microsoft/react-native-windows/issues/10197) | `transform` `translateX` throws with percentage values |
| [#1447](https://github.com/microsoft/react-native-windows/issues/1447) | Fractional flex values honoured on iOS/Android but not on this platform — 6 reactions |
| [#5437](https://github.com/microsoft/react-native-windows/issues/5437) | Certain components stopped rendering without an explicit `height` |

**Cause pattern.** A numeric style value has three failure modes — non-finite, wrong unit, out of domain — and a
platform typically checks none of them until something downstream (a serializer, a `SkMatrix`) explodes with a
message naming the wrong subsystem, thousands of frames from the actual cause.

**Applies fully.** Every numeric entering our scene from props or Yoga (`LayoutMetrics`, transforms, border
widths, radii, opacity, scroll offsets) needs a reject-at-the-boundary check; our ASan+UBSan CI jobs already
detect float-cast-overflow but nothing currently feeds them this class of adversarial input.

**Our coverage.** #11 (Yoga layout, closed), #13 (view props, closed). **Gap: one proposed issue** (`fix(core):
non-finite layout and style values`, M1, P1).

---

## Group 6 — Independent lifetimes with no enforced threading contract

| RNW issue | Gist |
| --- | --- |
| [#16309](https://github.com/microsoft/react-native-windows/issues/16309) | Permanent UI-thread hang: unbounded retry loop when a native-driver animation targets a view absent from the Fabric registry — the most instructive open bug for our M2 |
| [#4312](https://github.com/microsoft/react-native-windows/issues/4312) | `Animated.Value` does not increment properly — open, 11 comments |
| [#3283](https://github.com/microsoft/react-native-windows/issues/3283) | A missing facade type (`"progress"`) silently dropped a property — the driver didn't know the value type existed |
| [#9661](https://github.com/microsoft/react-native-windows/issues/9661) | `HermesRuntimeHolder` and `DevSettings` leaked on `UnloadInstance` |
| [#8010](https://github.com/microsoft/react-native-windows/issues/8010) | Crash with `RPC_E_WRONG_THREAD` destroying `UIManagerModule` off-thread — teardown *order* was wrong |
| [#10707](https://github.com/microsoft/react-native-windows/issues/10707) | Crash resolving a JS promise on the UI thread — a module author did the obvious thing and nothing stopped them |
| [#13925](https://github.com/microsoft/react-native-windows/issues/13925) | A TurboModule using JSI crashes with `this` at address `0x1` |
| [#1027](https://github.com/microsoft/react-native-windows/issues/1027) | `System.AccessViolationException` in Yoga — 34 comments, a decade-old thread with no root cause, which is what a missing threading contract looks like from outside |

**Cause pattern.** The animated-node graph, the mounting layer, and module threads each have independent
lifetimes; nothing forces agreement on "may this be called from this thread right now," and the natural failure
mode on the thread that draws is an unbounded retry — a hang, not a crash, and the hardest of the two to diagnose.

**Applies fully, and #16309 describes our exact configuration.** ADR-0001 decision 6 commits us to the shared C++
animated node graph driven from a platform-owned frame thread — a thread React Native itself has no concept of.
On our frame thread, an unbounded retry is unrecoverable: no frame callback, no `xdg_toplevel.close` handling, no
way out but SIGKILL.

**Our coverage.** #19 (native-driven animations, open), #20 (pacing, open), #9/#10 (Hermes/Fabric bootstrap,
closed), #4 (CI sanitizers, closed), #23 (native modules, open). **Gap: four proposed issues** — three at P0
(animation-hang guard M2, instance teardown leak gate M2, cross-thread contract stress M2) and one at P2
(Animated value conformance M2).

---

## Group 7 — The build itself is a first-class complaint

| RNW issue | Gist |
| --- | --- |
| [#8103](https://github.com/microsoft/react-native-windows/issues/8103) | "Reduce building react-native-windows memory and disk space requirements" — open, 4 reactions |
| [#9619](https://github.com/microsoft/react-native-windows/issues/9619) | "Building M.RN takes longer and sometimes breaks incrementality" — open, 13 comments, labelled `Recent Regression` |
| [#4787](https://github.com/microsoft/react-native-windows/issues/4787) | A new app includes 1.6 GB of unused packages |
| [#4775](https://github.com/microsoft/react-native-windows/issues/4775) | A user reports 12 GB for a project checkout |
| [#9518](https://github.com/microsoft/react-native-windows/issues/9518) | Autolinking regressed in speed and stayed regressed for a release |

**Cause pattern.** Build cost is a silent regression surface exactly like runtime behaviour, but nobody gates it,
so it drifts until a maintainer notices by irritation rather than by a number.

**Applies fully.** `docs/cpp-toolchain.md` already records three `native` CI entries at a 120-minute timeout each,
~113 MB of vendored caches, a 2 GB ccache ceiling, and a documented-but-untriggered escape hatch (move sanitizer
jobs to their own workflow "if the wall clock ever stops being tolerable"). That escape hatch needs a number to
trigger it, not a feeling.

**Our coverage.** #4 (CI, closed), #3 (toolchain, closed), #28 (quality toolchain, closed). **Gap: one proposed
issue** (`perf: build wall-clock, incrementality and disk budget`, M2, P1).

---

## Group 8 — The dev loop and release-mode divergence

| RNW issue | Gist |
| --- | --- |
| [#7451](https://github.com/microsoft/react-native-windows/issues/7451) | "Error on metro server after 'successfully' loading app" — 16 reactions, 31 comments, the loudest developer-experience bug in the repo |
| [#9510](https://github.com/microsoft/react-native-windows/issues/9510) | A 10 MB limit on `fetch` |
| [#12168](https://github.com/microsoft/react-native-windows/issues/12168) | Cannot upload files with form data |
| [#9407](https://github.com/microsoft/react-native-windows/issues/9407) | Hermes direct debugging never worked because `InstanceImpl` never called `StartInspector` — 26 comments, everything downstream reported a different symptom |
| [#16354](https://github.com/microsoft/react-native-windows/issues/16354) | The DevTools Performance panel disconnects the app when you stop a trace |
| [#8609](https://github.com/microsoft/react-native-windows/issues/8609) | "Add UI Tests Validating Live Reload/Fast Refresh" — still open |
| [#4779](https://github.com/microsoft/react-native-windows/issues/4779) / [#8608](https://github.com/microsoft/react-native-windows/issues/8608) | Fast Refresh broke on two separate releases, each found by a human |
| [#12258](https://github.com/microsoft/react-native-windows/issues/12258) | "Can't Run In Release Mode" — 19 comments |
| [#10255](https://github.com/microsoft/react-native-windows/issues/10255) | Offline-built bundles white-screen on launch (0.69+) — 15 comments, `must-have` + `Recent Regression` |
| [#11952](https://github.com/microsoft/react-native-windows/issues/11952) | `Array.prototype.flat()` breaks the app **in release builds only** |
| [#14741](https://github.com/microsoft/react-native-windows/issues/14741) | `"hello".toLocaleUpperCase()` and `Intl.getCanonicalLocales(['und'])` issues — 15 comments |

**Cause pattern.** Everything in this group works in the one configuration everyone tests (debug, dev server
attached) and silently diverges in a configuration nobody automates: release bytecode, a stopped debugger, an
edit that doesn't hot-reload, a locale nobody typed in English. Five separate subsystems, one shared cause: no
automated coverage of the *other* configuration.

**Applies fully, and is our largest unimplemented surface.** `docs/cpp-toolchain.md` states plainly there is no
networking stack at all (`ReactCxxPlatform`'s HTTP client needs nlohmann_json and OpenSSL, neither linked), that
`FallbackRuntimeTargetDelegate` means no CDP-backed console or profiler, and everything today runs a development
bundle from disk with no release path. ADR-0001 separately flags `Intl`/ECMA-402 as an accepted, unresolved risk
on Hermes-for-Linux.

**Our coverage.** #15 (`<Image>`, closed), #22 (CLI/Metro, open), #23 (modules, open), #9 (Hermes, closed), #7
(e2e driver, open), #4 (CI, closed), #30 (Hermes benchmark gate, open), #24 (flagship bring-up, open). **Gap:
five proposed issues** spanning M3 — networking/Metro dev server (P0), DevTools over CDP (P0), Fast Refresh proof
(P1), release-mode parity gate (P0), and the Intl/ECMA-402 decision (P1, `needs:decision`).

---

## Group 9 — Deforking and the tooling ledger

| RNW issue | Gist |
| --- | --- |
| [#3907](https://github.com/microsoft/react-native-windows/issues/3907) | "Build tool to help integrate diffs in platform overrides" — the origin of `react-native-platform-override` |
| [#5269](https://github.com/microsoft/react-native-windows/issues/5269) | Removing a fork of `StyleSheetValidation.js` once upstream made it unnecessary — the successful case, and it took a scheduled review to notice |
| [#3263](https://github.com/microsoft/react-native-windows/issues/3263) | "`react-native run-windows` command is failing at build step" — **63 comments** |
| [#2420](https://github.com/microsoft/react-native-windows/issues/2420) | "MSBuild tools not found" — 35 comments |
| [#16274](https://github.com/microsoft/react-native-windows/issues/16274) | `init-windows` fails silently with "unknown command" when `pwsh.exe` is missing, **not documented in their own dependency checker** — they have a doctor and it still missed a prerequisite |
| [#7566](https://github.com/microsoft/react-native-windows/issues/7566) | `requireNativeComponent "RNSScreen"` was not found in the UIManager — 23 comments; a registration failure presented as a missing component |

**Cause pattern.** `Deforking` alone carries **122 issues** — not a bug category but a decade-long programme to
*reduce* the number of forked upstream files, because every fork is a merge conflict on every release. Alongside
it, the highest-comment bugs in the whole tracker are not rendering bugs, they are "it will not build on my
machine," and even RNW's own dependency doctor missed a prerequisite it should have caught.

**Applies fully.** ADR-0001 already commits us to `react-native-platform-override` and sizes our fork at "roughly
thirteen core JS files," an obligation that "grows with every React Native release" — thirteen stays thirteen
only if something counts it. Our own prerequisite list (`docs/cpp-toolchain.md`) spans Vulkan, Wayland, xkbcommon,
a C++20 clang, CMake, Ninja, ccache, Python, Node across a distribution matrix nobody controls — a larger surface
than RNW's Windows-only toolchain.

**Our coverage.** #22 (platform registration, open), #21 (codegen/autolinking, open), #29 (Linux platform
identity, closed), #3 (toolchain, closed), #28 (quality toolchain, closed). **Gap: three proposed issues** — a
dependency preflight doctor (M3, P0), codegen determinism and spec coverage (M3, P0), and a platform-override
manifest with a validate gate (M3, P1).

---

## Group 10 — Ecosystem support is the loudest ask, not rendering

| RNW issue | Gist |
| --- | --- |
| [#13534](https://github.com/microsoft/react-native-windows/issues/13534) | "Expo support" — **27 reactions, the most-reacted open issue in the repository** |
| [#3326](https://github.com/microsoft/react-native-windows/issues/3326) | Support `react-native-svg` — 20 reactions |
| [#4151](https://github.com/microsoft/react-native-windows/issues/4151) | Support `react-native-reanimated` — 16 reactions |
| [#4140](https://github.com/microsoft/react-native-windows/issues/4140) | Support `react-native-gesture-handler` — 15 reactions |
| [#2829](https://github.com/microsoft/react-native-windows/issues/2829) | Support `react-native-fs` — open since 2019, 29 comments |
| [#6028](https://github.com/microsoft/react-native-windows/issues/6028) | WebRTC support — closed `Won't Fix`, an explicit scope decision rather than silence |
| [#13250](https://github.com/microsoft/react-native-windows/issues/13250) | Align the new-architecture C++ library template with the React Native "golden" template |
| [#13884](https://github.com/microsoft/react-native-windows/issues/13884) | The library template fails on a library created by `create-react-native-library`, the tool every library author actually uses |
| [#4540](https://github.com/microsoft/react-native-windows/issues/4540) | "Split up `Microsoft.ReactNative.dll` into two DLLs: Core and UWP" — open, anchor of the 63-issue `Area: Core DLL` label |
| [#15608](https://github.com/microsoft/react-native-windows/issues/15608) | NuGet packages missing for a point release — the distribution channel is itself a failure mode |

**Cause pattern.** Sorted by reactions, RNW's tracker is not a list of rendering bugs — ten years in, Expo support
is still the top open request. A platform whose only module author is its own team has an ecosystem of one, and
an out-of-date or template-incompatible authoring path (`#13884`) is why.

**Applies fully.** ADR-0001's M4 already names roughly thirty flagship dependencies plus the explicit
port-or-decline decision on Reanimated — a decision no out-of-tree platform, Microsoft or Huawei, has ever made.
ADR-0001 also records that Skia is vendored at a git hash with no ABI, mirroring RNW's multi-year Core-DLL/ABI
programme.

**Our coverage.** #24 (flagship bring-up, open), #21/#22/#23 (codegen, platform registration, core modules, all
open), #25 (AUR/omarchy-pkgs packaging, open), #3 (toolchain, closed). **Gap: three proposed issues** — an
ecosystem support matrix (M4, P0), a library template and authoring path (M4, P1), and a `needs:decision` on
prebuilt core binaries and an ABI boundary (M4, P2).

---

## Group 11 — Accessibility is the single largest defect category in the mature tracker

| RNW issue | Gist |
| --- | --- |
| [#9826](https://github.com/microsoft/react-native-windows/issues/9826) | "Keyboard Focus and Accessibility Focus should be the same" — they diverged, and everything downstream inherited the divergence |
| [#9292](https://github.com/microsoft/react-native-windows/issues/9292) | `ref.focus()` does not work after the first render — "requires ugly workaround," 15 comments |
| [#6566](https://github.com/microsoft/react-native-windows/issues/6566) | Cannot tab into `SectionList` headers |
| [#3006](https://github.com/microsoft/react-native-windows/issues/3006) | Cannot tab in or out of a popup |
| [#14426](https://github.com/microsoft/react-native-windows/issues/14426) | Narrator gives no audio after initial focus on New Architecture apps — the tree existed, the *events* did not |
| [#3836](https://github.com/microsoft/react-native-windows/issues/3836) | `accessibilityLabel` on a `<View>` does nothing without also setting `accessibilityRole` — a mapping rule nobody could have guessed |
| [#6706](https://github.com/microsoft/react-native-windows/issues/6706) / [#10259](https://github.com/microsoft/react-native-windows/issues/10259) | Setting `borderRadius` silently removes `accessibilityLabel`/the announcement — a purely visual prop deleting a node from the a11y tree, twice |
| [#10484](https://github.com/microsoft/react-native-windows/issues/10484) | Changing `importantForAccessibility` re-mounts all children recursively — labelled both `Area: Accessibility` and `Area: Performance` |

**Cause pattern.** `Area: Accessibility` currently carries **297 issues** (confirmed live; windows.json's own
citations put it at 253 when written — the label keeps growing), larger than `Area: TextInput` (190, was 148) or
`Area: Text` (149, was 121). In the most mature out-of-tree React Native platform, accessibility is the single
biggest body of defects, split evenly between keyboard operability (focus divergence, trapped focus, `ref.focus()`
regressing after mount) and dynamic AT-SPI/Narrator events (a visual style prop silently deleting a node from the
tree).

**Applies fully.** ADR-0001 already calls accessibility a graveyard; this tracker quantifies exactly why, and the
`#6706`/`#10259` pattern is structurally guaranteed for us because our accessible geometry is derived from the
same `RetainedScene` that owns `borderRadius` and clipping — a style change and an accessibility change share one
code path by construction, not by accident.

**Our coverage.** #27 (AT-SPI groundwork, open), #18 (input pipeline, closed), #16 (`<ScrollView>`, open), #13
(view props, closed), #17 (`<TextInput>`, open), #61 (AT-SPI role/name mapping conformance, open — the macOS
plan's contribution). **Gap: two proposed issues** — a keyboard-only operability gate (M5, P1) and dynamic
announcements/focus/caret events plus text scale and high contrast (M5, P1).

---

## Group 12 — Test infrastructure has to out-run the feature set

| RNW issue | Gist |
| --- | --- |
| [#14069](https://github.com/microsoft/react-native-windows/issues/14069) | "[Modal] Test if our current testing infrastructure can handle multiple windows" — filed *after* shipping `Modal`, when the harness turned out to only see one window |
| [#15165](https://github.com/microsoft/react-native-windows/issues/15165) | App crashes after creating a `Modal` and moving the window |
| [#8251](https://github.com/microsoft/react-native-windows/issues/8251) | Prevent window focus stealing for auto-focus scenarios — focus becomes a *global* resource once there are two windows |
| [#885](https://github.com/microsoft/react-native-windows/issues/885) | "Support multi-window apps" — the feature request the bugs above are the price of |
| [#6681](https://github.com/microsoft/react-native-windows/issues/6681) | "Provide better ways to analyze React Native performance on Windows" — open, 9 reactions, filed by Microsoft against itself |
| [#8156](https://github.com/microsoft/react-native-windows/issues/8156) | Gradual slowdown calling a native method from JS — a leak that presents as a perf complaint, found by a user, not a tool |

**Cause pattern.** Building a feature before the test harness can observe it is how `#14069` happened: `Modal`
shipped, and only then did anyone discover the harness could address one window. Separately, `#6681` shows that
even the platform's own maintainers lack an application-developer-facing way to answer "why is my screen slow,"
which is a different problem from the CI frame-time gate the *platform* team already has.

**Applies fully.** Our harness today drives one `rnl_window` under `weston --backend=headless`, screenshots one
surface, injects input to one seat — building multi-window (#62, open) or a developer-facing perf surface before
extending the harness reproduces `#14069` on purpose.

**Our coverage.** #7 (e2e driver, open), #33 (window golden, closed), #6 (golden rig, closed), #20 (pacing/Tracy,
open), #30 (Hermes benchmark gate, open), #62 (Modal/overlay decision, open — the macOS plan's contribution).
**Gap: two proposed issues** — the e2e harness must drive more than one window (M5, P1) and an app-developer
performance surface (M4, P3).

---

## Group size summary

| # | Group | Proposed issues | RNW cited | Applies to us |
| --- | --- | --- | --- | --- |
| 1 | Event payload identity | 3 | 13 | fully |
| 2 | CSS-shaped visual props | 2 | 3 | fully — the ADR-0001 canvas argument |
| 3 | Prop coverage as process | 1 | 5 | fully — highest leverage |
| 4 | Text: fonts, subpixel, RTL | 3 | 10 | fully |
| 5 | Numeric hygiene | 1 | 4 | fully |
| 6 | Independent-lifetime threading | 4 | 17 | fully — our exact M2 configuration |
| 7 | Build cost | 1 | 7 | fully |
| 8 | Dev loop / release divergence | 5 | 33 | fully — largest unimplemented surface |
| 9 | Deforking and tooling ledger | 3 | 19 | fully |
| 10 | Ecosystem support | 3 | 22 | fully |
| 11 | Accessibility | 2 | 20 | fully — largest defect category in the mature tracker |
| 12 | Test infrastructure catch-up | 2 | 13 | fully |

Totals: 30 proposed issues, 155 distinct RNW issues cited (some numbers cited in more than one group — e.g.
`#8103`/`#4787` anchor both build cost and prebuilt binaries, `#13183`/`#12641`/`#13884` anchor both prop coverage
and the library template — so per-group counts sum to more than 155).

---

## Mapping to our epic

Existing sub-issues of [#1, the v0.1 epic](https://github.com/react-native-linux/react-native-linux/issues/1), by
milestone (`gh issue list --state all`, 2026-09-02):

| Milestone | Existing sub-issues | This batch adds |
| --- | --- | --- |
| M0 (bootstrap) | 13 (#2–#11, #28, #29, #31, #32) | 0 — nothing in windows.json targets M0 |
| M1 (renderer/text/input parity) | 31 | 10 |
| M2 (animation, pacing, threading) | 7 | 5 |
| M3 (dev loop, tooling, CLI) | 8 | 8 |
| M4 (ecosystem, packaging, flagship) | 3 | 4 |
| M5 (accessibility, multi-window) | 3 | 3 |

M1 already carries the macOS-derived batch (#35–#54 and neighbours) plus the original scaffolding issues; this
batch does not compete with that work, it targets props and subsystems the macOS study had no reason to surface
(CSS-shaped visual props, font asset registration, RTL, non-finite numeric hygiene) because AppKit either provides
them or never grew that CSS surface in the first place. M3 is proportionally the largest addition (8 new issues
against 8 existing) because dev-loop/release-mode divergence and deforking tooling are RNW-scale lessons: they
only became visible once a platform survived long enough to accumulate a decade of "works in debug, not in
release" reports, which macOS's smaller, AppKit-backed tracker never generated at the same volume.

---

## Proposed issues

Full bodies live in `scripts/issue-plans/windows.json`. Title, priority, milestone, and the RNW issue numbers each
body cites as evidence:

| Title | Priority | Milestone | RNW sources |
| --- | --- | --- | --- |
| feat(input): pointerEvents semantics — auto, none, box-none, box-only, rounded-corner hit shape | P1 | M1 | [#8496](https://github.com/microsoft/react-native-windows/issues/8496), [#8531](https://github.com/microsoft/react-native-windows/issues/8531), [#9767](https://github.com/microsoft/react-native-windows/issues/9767), [#10493](https://github.com/microsoft/react-native-windows/issues/10493), [#16316](https://github.com/microsoft/react-native-windows/issues/16316) |
| feat(input): keyboard event identity — key vs. code vs. keysym, isComposing, repeat | P1 | M1 | [#2469](https://github.com/microsoft/react-native-windows/issues/2469), [#5821](https://github.com/microsoft/react-native-windows/issues/5821), [#11049](https://github.com/microsoft/react-native-windows/issues/11049), [#16088](https://github.com/microsoft/react-native-windows/issues/16088) |
| feat(input): mouse button identity — button/buttons, secondary and middle | P2 | M1 | [#688](https://github.com/microsoft/react-native-windows/issues/688), [#6410](https://github.com/microsoft/react-native-windows/issues/6410), [#8275](https://github.com/microsoft/react-native-windows/issues/8275), [#8499](https://github.com/microsoft/react-native-windows/issues/8499) |
| feat(renderer): boxShadow and the shadow props on the canvas | P1 | M1 | [#2796](https://github.com/microsoft/react-native-windows/issues/2796), [#2800](https://github.com/microsoft/react-native-windows/issues/2800) |
| feat(renderer): filter, mixBlendMode, isolation and outline | P2 | M1 | [#2800](https://github.com/microsoft/react-native-windows/issues/2800), [#15352](https://github.com/microsoft/react-native-windows/issues/15352) |
| test(renderer): prop-coverage conformance — every declared prop asserted | P0 | M1 | [#4037](https://github.com/microsoft/react-native-windows/issues/4037), [#6227](https://github.com/microsoft/react-native-windows/issues/6227), [#11152](https://github.com/microsoft/react-native-windows/issues/11152), [#13183](https://github.com/microsoft/react-native-windows/issues/13183), [#15352](https://github.com/microsoft/react-native-windows/issues/15352) |
| feat(text): font asset registration — bundled assets, loud failure | P1 | M1 | [#3463](https://github.com/microsoft/react-native-windows/issues/3463), [#3966](https://github.com/microsoft/react-native-windows/issues/3966), [#4467](https://github.com/microsoft/react-native-windows/issues/4467), [#16308](https://github.com/microsoft/react-native-windows/issues/16308) |
| test(text): glyph rasterization policy — no subpixel fringing on alpha, gamma pinned | P1 | M1 | [#16340](https://github.com/microsoft/react-native-windows/issues/16340) |
| feat(text): RTL — writingDirection, base paragraph direction, I18nManager without reload | P1 | M1 | [#4167](https://github.com/microsoft/react-native-windows/issues/4167), [#4432](https://github.com/microsoft/react-native-windows/issues/4432), [#7070](https://github.com/microsoft/react-native-windows/issues/7070), [#7792](https://github.com/microsoft/react-native-windows/issues/7792), [#12716](https://github.com/microsoft/react-native-windows/issues/12716) |
| fix(core): non-finite layout and style values rejected at the boundary | P1 | M1 | [#1447](https://github.com/microsoft/react-native-windows/issues/1447), [#5437](https://github.com/microsoft/react-native-windows/issues/5437), [#8318](https://github.com/microsoft/react-native-windows/issues/8318), [#10197](https://github.com/microsoft/react-native-windows/issues/10197) |
| fix(core): a native-driver animation targeting an unmounted view must not hang the frame thread | P0 | M2 | [#2297](https://github.com/microsoft/react-native-windows/issues/2297), [#3283](https://github.com/microsoft/react-native-windows/issues/3283), [#4312](https://github.com/microsoft/react-native-windows/issues/4312), [#16309](https://github.com/microsoft/react-native-windows/issues/16309) |
| test(core): Animated value conformance — units, interpolation, additive composition | P2 | M2 | [#3283](https://github.com/microsoft/react-native-windows/issues/3283), [#4312](https://github.com/microsoft/react-native-windows/issues/4312), [#9155](https://github.com/microsoft/react-native-windows/issues/9155), [#10197](https://github.com/microsoft/react-native-windows/issues/10197) |
| test(core): instance teardown and reload leak gate | P0 | M2 | [#1783](https://github.com/microsoft/react-native-windows/issues/1783), [#5002](https://github.com/microsoft/react-native-windows/issues/5002), [#5497](https://github.com/microsoft/react-native-windows/issues/5497), [#8010](https://github.com/microsoft/react-native-windows/issues/8010), [#8156](https://github.com/microsoft/react-native-windows/issues/8156), [#9661](https://github.com/microsoft/react-native-windows/issues/9661), [#12247](https://github.com/microsoft/react-native-windows/issues/12247) |
| test(core): cross-thread contract stress — promise resolved off the JS thread | P0 | M2 | [#1027](https://github.com/microsoft/react-native-windows/issues/1027), [#7354](https://github.com/microsoft/react-native-windows/issues/7354), [#8010](https://github.com/microsoft/react-native-windows/issues/8010), [#10707](https://github.com/microsoft/react-native-windows/issues/10707), [#13925](https://github.com/microsoft/react-native-windows/issues/13925) |
| perf: build wall-clock, incrementality and disk budget | P1 | M2 | [#4004](https://github.com/microsoft/react-native-windows/issues/4004), [#4649](https://github.com/microsoft/react-native-windows/issues/4649), [#4775](https://github.com/microsoft/react-native-windows/issues/4775), [#4787](https://github.com/microsoft/react-native-windows/issues/4787), [#8103](https://github.com/microsoft/react-native-windows/issues/8103), [#9518](https://github.com/microsoft/react-native-windows/issues/9518), [#9619](https://github.com/microsoft/react-native-windows/issues/9619) |
| feat(core): Metro dev server and an HTTP networking stack | P0 | M3 | [#2460](https://github.com/microsoft/react-native-windows/issues/2460), [#7451](https://github.com/microsoft/react-native-windows/issues/7451), [#8642](https://github.com/microsoft/react-native-windows/issues/8642), [#9510](https://github.com/microsoft/react-native-windows/issues/9510), [#10036](https://github.com/microsoft/react-native-windows/issues/10036), [#10062](https://github.com/microsoft/react-native-windows/issues/10062), [#11439](https://github.com/microsoft/react-native-windows/issues/11439), [#12168](https://github.com/microsoft/react-native-windows/issues/12168) |
| feat(core): React Native DevTools over CDP | P0 | M3 | [#9359](https://github.com/microsoft/react-native-windows/issues/9359), [#9407](https://github.com/microsoft/react-native-windows/issues/9407), [#12654](https://github.com/microsoft/react-native-windows/issues/12654), [#14037](https://github.com/microsoft/react-native-windows/issues/14037), [#15349](https://github.com/microsoft/react-native-windows/issues/15349), [#16354](https://github.com/microsoft/react-native-windows/issues/16354) |
| test(core): Fast Refresh and reload proven by a UI test | P1 | M3 | [#3774](https://github.com/microsoft/react-native-windows/issues/3774), [#3822](https://github.com/microsoft/react-native-windows/issues/3822), [#4062](https://github.com/microsoft/react-native-windows/issues/4062), [#4385](https://github.com/microsoft/react-native-windows/issues/4385), [#4779](https://github.com/microsoft/react-native-windows/issues/4779), [#8608](https://github.com/microsoft/react-native-windows/issues/8608), [#8609](https://github.com/microsoft/react-native-windows/issues/8609) |
| test(core): release-mode parity — bytecode, source maps, release smoke gate | P0 | M3 | [#7855](https://github.com/microsoft/react-native-windows/issues/7855), [#9732](https://github.com/microsoft/react-native-windows/issues/9732), [#10255](https://github.com/microsoft/react-native-windows/issues/10255), [#11952](https://github.com/microsoft/react-native-windows/issues/11952), [#12258](https://github.com/microsoft/react-native-windows/issues/12258), [#12708](https://github.com/microsoft/react-native-windows/issues/12708), [#13636](https://github.com/microsoft/react-native-windows/issues/13636) |
| feat(core): Intl/ECMA-402 stance — PlatformIntlICU vs. polyfills | P1 | M3 | [#2657](https://github.com/microsoft/react-native-windows/issues/2657), [#3742](https://github.com/microsoft/react-native-windows/issues/3742), [#4791](https://github.com/microsoft/react-native-windows/issues/4791), [#10272](https://github.com/microsoft/react-native-windows/issues/10272), [#14741](https://github.com/microsoft/react-native-windows/issues/14741) |
| feat(cli): dependency preflight doctor | P0 | M3 | [#2420](https://github.com/microsoft/react-native-windows/issues/2420), [#3263](https://github.com/microsoft/react-native-windows/issues/3263), [#10236](https://github.com/microsoft/react-native-windows/issues/10236), [#11642](https://github.com/microsoft/react-native-windows/issues/11642), [#13339](https://github.com/microsoft/react-native-windows/issues/13339), [#16274](https://github.com/microsoft/react-native-windows/issues/16274) |
| test(cli): codegen determinism and spec coverage | P0 | M3 | [#7566](https://github.com/microsoft/react-native-windows/issues/7566), [#12641](https://github.com/microsoft/react-native-windows/issues/12641), [#13183](https://github.com/microsoft/react-native-windows/issues/13183), [#13884](https://github.com/microsoft/react-native-windows/issues/13884), [#13925](https://github.com/microsoft/react-native-windows/issues/13925) |
| feat(cli): platform-override manifest with validate gate | P1 | M3 | [#3907](https://github.com/microsoft/react-native-windows/issues/3907), [#4222](https://github.com/microsoft/react-native-windows/issues/4222), [#4592](https://github.com/microsoft/react-native-windows/issues/4592), [#4678](https://github.com/microsoft/react-native-windows/issues/4678), [#5269](https://github.com/microsoft/react-native-windows/issues/5269), [#7231](https://github.com/microsoft/react-native-windows/issues/7231), [#10619](https://github.com/microsoft/react-native-windows/issues/10619), [#11961](https://github.com/microsoft/react-native-windows/issues/11961) |
| feat(modules): ecosystem support matrix | P0 | M4 | [#2829](https://github.com/microsoft/react-native-windows/issues/2829), [#3278](https://github.com/microsoft/react-native-windows/issues/3278), [#3326](https://github.com/microsoft/react-native-windows/issues/3326), [#3884](https://github.com/microsoft/react-native-windows/issues/3884), [#4140](https://github.com/microsoft/react-native-windows/issues/4140), [#4151](https://github.com/microsoft/react-native-windows/issues/4151), [#5173](https://github.com/microsoft/react-native-windows/issues/5173), [#6028](https://github.com/microsoft/react-native-windows/issues/6028), [#12567](https://github.com/microsoft/react-native-windows/issues/12567), [#13534](https://github.com/microsoft/react-native-windows/issues/13534), [#15078](https://github.com/microsoft/react-native-windows/issues/15078) |
| feat(cli): library template and third-party module authoring path | P1 | M4 | [#8321](https://github.com/microsoft/react-native-windows/issues/8321), [#12641](https://github.com/microsoft/react-native-windows/issues/12641), [#12786](https://github.com/microsoft/react-native-windows/issues/12786), [#13250](https://github.com/microsoft/react-native-windows/issues/13250), [#13884](https://github.com/microsoft/react-native-windows/issues/13884), [#15078](https://github.com/microsoft/react-native-windows/issues/15078) |
| build(packaging): prebuilt core binaries and a stated ABI boundary | P2 | M4 | [#4540](https://github.com/microsoft/react-native-windows/issues/4540), [#4552](https://github.com/microsoft/react-native-windows/issues/4552), [#4787](https://github.com/microsoft/react-native-windows/issues/4787), [#8103](https://github.com/microsoft/react-native-windows/issues/8103), [#12970](https://github.com/microsoft/react-native-windows/issues/12970), [#15608](https://github.com/microsoft/react-native-windows/issues/15608) |
| perf: an app-developer-facing performance surface | P3 | M4 | [#4872](https://github.com/microsoft/react-native-windows/issues/4872), [#6681](https://github.com/microsoft/react-native-windows/issues/6681), [#8024](https://github.com/microsoft/react-native-windows/issues/8024), [#8156](https://github.com/microsoft/react-native-windows/issues/8156), [#10484](https://github.com/microsoft/react-native-windows/issues/10484) |
| test(a11y): keyboard-only operability gate | P1 | M5 | [#2107](https://github.com/microsoft/react-native-windows/issues/2107), [#3006](https://github.com/microsoft/react-native-windows/issues/3006), [#4703](https://github.com/microsoft/react-native-windows/issues/4703), [#6181](https://github.com/microsoft/react-native-windows/issues/6181), [#6566](https://github.com/microsoft/react-native-windows/issues/6566), [#6765](https://github.com/microsoft/react-native-windows/issues/6765), [#9292](https://github.com/microsoft/react-native-windows/issues/9292), [#9826](https://github.com/microsoft/react-native-windows/issues/9826), [#10254](https://github.com/microsoft/react-native-windows/issues/10254), [#16363](https://github.com/microsoft/react-native-windows/issues/16363) |
| feat(a11y): announcements, focus and caret events, text scale, high contrast | P1 | M5 | [#3836](https://github.com/microsoft/react-native-windows/issues/3836), [#5177](https://github.com/microsoft/react-native-windows/issues/5177), [#6706](https://github.com/microsoft/react-native-windows/issues/6706), [#10253](https://github.com/microsoft/react-native-windows/issues/10253), [#10259](https://github.com/microsoft/react-native-windows/issues/10259), [#10484](https://github.com/microsoft/react-native-windows/issues/10484), [#11476](https://github.com/microsoft/react-native-windows/issues/11476), [#12981](https://github.com/microsoft/react-native-windows/issues/12981), [#14099](https://github.com/microsoft/react-native-windows/issues/14099), [#14426](https://github.com/microsoft/react-native-windows/issues/14426) |
| test(harness): the e2e harness must drive more than one window | P1 | M5 | [#618](https://github.com/microsoft/react-native-windows/issues/618), [#885](https://github.com/microsoft/react-native-windows/issues/885), [#4923](https://github.com/microsoft/react-native-windows/issues/4923), [#8251](https://github.com/microsoft/react-native-windows/issues/8251), [#12247](https://github.com/microsoft/react-native-windows/issues/12247), [#12927](https://github.com/microsoft/react-native-windows/issues/12927), [#14069](https://github.com/microsoft/react-native-windows/issues/14069), [#15165](https://github.com/microsoft/react-native-windows/issues/15165) |

None of the 30 duplicates an existing sub-issue; where an existing issue already owns the feature, the body scopes
the new issue to verification and cites it by number (the "Our coverage" line in each group above).

---

## Label taxonomy: what RNW's five overlapping dimensions teach

`gh api repos/microsoft/react-native-windows/labels` returns roughly 140 labels. Five genuinely distinct axes are
buried in that list, plus several that are one-off or archival:

| Their axis | Examples | What it tells us |
| --- | --- | --- |
| `Area: *` | ~90 labels, one per component or subsystem (`Area: FlatList`, `Area: Picker`, `Area: Focus`, `Area: Globalization`, …) | Fine-grained enough to be precise, too numerous to apply consistently — some issues carry three, most carry the one the filer happened to think of first. Our nine `area:*` labels stay coarse on purpose. |
| `Workstream: *` | `Accessibility`, `Component Parity`, `Developer Experience`, `Performance`, `Test Coverage`, `New Arch C# Support`, `New Arch NuGet Refactor` | A *cross-cutting initiative* axis, orthogonal to `Area:` — a workstream spans many areas over a bounded time. This is the one RNW-only concept worth borrowing conceptually: our milestones (M0–M5) already serve this purpose, so we do not need a separate label, but the *reason* to have one — "which multi-quarter initiative does this belong to" — is real and is currently implicit in milestone assignment alone. |
| `Partner: *` | `Facebook`, `Microsoft`, `Office`, `Stream`, `Xbox`, `AppConsult`, `3P` | Provenance of the *ask*, not the code. Meaningless for a one-contributor repository — correctly not adopted, same as the macOS study's conclusion. |
| `Platform: *` | `Desktop`, `UWP`, `Xbox`, `WinAppSDK`, `NetUI` | RNW ships one React Native fork across four host surfaces; the label disambiguates which one an issue is about. We ship one fork on one host surface (Wayland), so this axis collapses to nothing for us — evidence that our simpler target is a real structural advantage, not just a smaller scope. |
| `Parity: *` / `Scenario: *` | `Parity: Fabric vs. Paper`, `Parity: React Native`, `Parity: Windows`; `Scenario: ARM64`, `Scenario: Reload`, `Scenario: Upgrade` | Two different "what does correct mean" and "under what condition" axes, created after the fact because prose could no longer carry the distinction (see Group 3). Our `kind:parity` plus `platform-parity:*` already fold both `Parity:` variants into one dimension; `Scenario:` has no equivalent yet and we do not need one until a specific scenario (e.g., ARM64) becomes real. |

Also present and deliberately **not** adopted, matching the macOS study's conclusions: `no-recent-activity` /
`Stale`-equivalents (a bot label that would close issues like the seven-year-open `#2800` while they are still
valid), `good first issue` / `help wanted` (premature for a one-contributor repository), and `Resolution: *`
(`Duplicate`, `Won't Fix`, `For StackOverflow`) — a closing-reason taxonomy that is useful at RNW's comment volume
and pure overhead at ours.

`windows.json` extends the taxonomy `macos.json` established with exactly three additions, each earning its place
from a specific RNW pattern above rather than being copied wholesale: `kind:regression` (RNW's `Recent Regression`
label recurs across Groups 7 and 8 — `#9619`, `#10255` — as the single most diagnostic tag in their whole set,
because it tells a triager "this used to work" without reading the thread); `origin:flagship` (reserved — RNW has
no equivalent, but the ecosystem-support-matrix lesson in Group 10 implies that once the flagship app is running
end to end, some of our own bugs will come from playing Suuudokuuu rather than from a research batch, and those
need to stay distinguishable from `origin:rn-windows`/`origin:rn-macos`); and `platform-parity:host-linux` (used
in Groups 4, 11, and 12 above — RNW has no analogue because Windows itself is always the oracle for a Windows
desktop convention, but we have no desktop-convention oracle among the mobile-first React Native platforms, so
the label exists to mark exactly the issues where none of iOS/Android/macOS/Windows is the right reference).
