# Issue mining, second pass: what the first three passes did not reach

- Research report, 2026-09-05. Read-only; nothing here has been filed. Proposed sub-issues sit under the milestone
  epics #172 (M0) … #177 (M5) and #209 (test-suite parity).
- Method. The three earlier passes (`react-native-macos-issues.md`, `react-native-windows-issues.md`,
  `react-native-core-issues.md`) plus the four issue plans cite **562 distinct upstream issue URLs**; that set was
  extracted with a single grep and used as a blocklist, so every upstream link below is one the tracker has not
  seen before unless the entry says otherwise. The prior core pass listed its 121 queries; this pass deliberately
  ran the topics that list does not contain (accessibility, `selectable`, `letterSpacing`/`textTransform`, emoji
  and font fallback, placeholders, snapping/paging, `contentInset`, `maintainVisibleContentPosition`,
  `scrollToIndex`/`scrollToEnd`, inverted lists, `RefreshControl`, GIF/animated images, `requestAnimationFrame`,
  `Pressable` under scroll, `locationX`, `Appearance.setColorScheme`, `Switch`/`ActivityIndicator`, `Alert`),
  then swept the open `Area: Fabric`, `Area: Text`, `Area: Image`, `Area: Layout`, `Area: Mouse` and
  `Area: Accessibility` labels of react-native-windows, the open and newest tails of react-native-macos, and —
  for the first time — `necolas/react-native-web` by topic.
- Corpora actually reached: `react/react-native` (`created:>=2022-01-01`, the Fabric era), `microsoft/react-native-windows`,
  `microsoft/react-native-macos`, `necolas/react-native-web`. **Not reached:** `OpenHarmony-SIG/ohos_react_native`
  (the search API returns 422 — the repository is not on GitHub), and `react-native-skia/react-native-skia`, whose
  entire tracker is 66 issues of which the highest-reacted is "Add svg support" (+5) and "Is the project dead?"
  (+1) — nothing to mine. Reaction (`+n`) and comment (`cN`) counts are the API values on 2026-09-05.
- Dedupe. Every candidate was grepped against the bodies of all open issues (`gh issue list --json body`) for its
  key terms, and the relevant issue bodies (#16, #17, #37, #45, #48, #53, #54, #61, #91, #109, #216) were read. A
  DEDUPE line names what already covers part of a candidate; candidates that were fully covered are listed at the
  end as *dropped*, with the upstream numbers kept so they can be pasted into the owning issue as amendments.
- Result: **27 candidates** kept, 14 dropped as covered. Priorities follow the existing `priority:P0–P3` scheme.

---

## Group A — ScrollView: the semantics `UIScrollView` gives away and the canvas has to write

ADR-0001 already says the physics will "feel wrong for a long time". This group is the set of `ScrollView` props
that are *not* physics and that no existing issue names: they are arithmetic on the offset, and each one has a
landed upstream fix or an open desktop-platform gap to copy from.

### A1. Snapping and paging

- [core#48393](https://github.com/react/react-native/issues/48393) — open, +6, c8: `snapToInterval`/`snapToOffsets`
  do nothing when the ScrollView width is fractional (a float-comparison bug in the settle target).
- [core#37448](https://github.com/react/react-native/issues/37448) — closed, +1, c9: layout shift with
  `snapToInterval` + `getItemLayout` + `ItemSeparatorComponent`.
- [core#33925](https://github.com/react/react-native/issues/33925) — closed, +1, c9: `onScroll` is not smooth on a
  page change with `pagingEnabled` even under the native driver.
- [web#1250](https://github.com/necolas/react-native-web/issues/1250) — open, +14, c2: web maps `pagingEnabled` to
  CSS `scroll-snap-align` and users want control of the snap point.
- [rnw#13150](https://github.com/microsoft/react-native-windows/issues/13150) — open, +1, c1: implement
  `snapToInterval` for Fabric; [rnw#13144](https://github.com/microsoft/react-native-windows/issues/13144) — open:
  `pagingEnabled`; [rnw#13793](https://github.com/microsoft/react-native-windows/issues/13793) — open:
  `disableIntervalMomentum`.

**Why it applies.** On the canvas the settle target of a flick is a number our physics computes; `snapToInterval`,
`snapToOffsets`, `snapToAlignment`, `snapToStart/End`, `pagingEnabled` and `disableIntervalMomentum` are a
function from (offset, velocity) to that target and nothing else. react-native-windows has three open issues
because the widget it wraps has no such hook; we have no widget, so the function is ours to write and to prove —
including [core#48393](https://github.com/react/react-native/issues/48393)'s fractional width, which is the *normal*
case under `wp_fractional_scale_v1`.

- **Epic:** #173 (M1) — it is a `ScrollView` prop set, owned by #16.
- **DEDUPE:** #16 owns momentum and deceleration but its acceptance criteria do not mention snapping or paging;
  #48 (wheel vs touchpad) and #109 (programmatic scroll) do not either. No overlap beyond the physics they share.
- **Proposed title:** `feat(renderer): snapToInterval, snapToOffsets, pagingEnabled and disableIntervalMomentum — the settle target is part of the physics`
- **Acceptance:** unit (100 % gate) — settle-target function over a table of (offset, velocity, interval/offsets,
  alignment, fractional widths); e2e — a wheel flick and a touchpad flick each end on a snap point with the momentum
  bracket intact; golden — a paged carousel at rest on page 2 under a 1.25 fractional scale.

### A2. `maintainVisibleContentPosition`

- [core#58186](https://github.com/react/react-native/issues/58186) — open, 2026-08-28: one-frame jump on prepend on
  Android.
- [core#42915](https://github.com/react/react-native/issues/42915) — open, +2, c7: incorrectly auto-scrolls on iOS.
- [core#42905](https://github.com/react/react-native/issues/42905) — open, +3, c3: unmounting the list header while
  the prop is active leaves white space.
- [core#53542](https://github.com/react/react-native/issues/53542) — closed, +2, c4: fails under rapid data updates;
  [core#52757](https://github.com/react/react-native/issues/52757) — closed, +2, c6: removing from the start scrolls.
- [core#41212](https://github.com/react/react-native/issues/41212) — open, c3: jumps while sliding.
- [rnw#13798](https://github.com/microsoft/react-native-windows/issues/13798) — open, +1: not implemented for Fabric.

**Why it applies.** Upstream implements this in the platform's mounting layer: on the commit that inserts children
before the anchor, the offset is adjusted *in the same transaction*, otherwise the user sees exactly the one-frame
jump [core#58186](https://github.com/react/react-native/issues/58186) reports. Our mounting layer applies
`MountingTransaction`s and our first-frame proofs (#222/#223) already assert "no intermediate frame"; this is the
same discipline applied to a scroll offset. Chat-style and log-style lists in any desktop app depend on it.

- **Epic:** #173 (M1).
- **DEDUPE:** #109's context line says the prop is *parsed* and then stops; no acceptance criterion anywhere
  covers it. ADR-0001 names it in the accepted-risk paragraph.
- **Proposed title:** `feat(renderer): maintainVisibleContentPosition — a prepend adjusts the offset in the same commit, never one frame later`
- **Acceptance:** unit (100 % gate) — offset adjustment computed from the anchor's pre/post layout in one
  transaction, including `minIndexForVisible` and `autoscrollToTopThreshold`; e2e — a prepend of N rows under
  the harness's frame capture shows zero frames with the anchor displaced; TSan-clean because it crosses the
  commit and render threads.

### A3. `contentInset`, `contentOffset` and the adjusted content end

- [core#54123](https://github.com/react/react-native/issues/54123) — closed, +14, c4: as of 0.81 scrolling *on the
  inset area* does nothing — the inset region stopped being hit-testable.
- [core#57522](https://github.com/react/react-native/issues/57522) — open, 2026-07: `scrollToEnd` ignores
  `adjustedContentInset` on Fabric iOS.
- [core#47959](https://github.com/react/react-native/issues/47959) — open, +2, c4: `scrollToEnd` scrolls too far on
  Android.
- [core#57988](https://github.com/react/react-native/issues/57988) — open, 2026-08; [core#57314](https://github.com/react/react-native/issues/57314)
  — closed, c3: a recycled ScrollView keeps or loses a non-zero `contentInset` (the recycled-state cluster).
- [core#36641](https://github.com/react/react-native/issues/36641) — closed, c2: `RefreshControl` cut off under
  `contentInset` with `automaticallyAdjustContentInsets` off.
- [web#1273](https://github.com/necolas/react-native-web/issues/1273) — open, +4, c5: `contentOffset` prop unsupported.

**Why it applies.** The inset extends the scrollable and hit-testable region beyond the content box; on a canvas
that is a rectangle we choose, and [core#54123](https://github.com/react/react-native/issues/54123) is precisely
what happens when the hit rectangle is the content box instead. `scrollToEnd` must target
`contentSize + inset.bottom − viewport`, and the initial `contentOffset` is applied before the first frame or the
first frame is wrong.

- **Epic:** #173 (M1).
- **DEDUPE:** #49 mentions `contentInset` only as the scroll-indicator inset; #109 owns `scrollTo`/`scrollToEnd`
  *not feeding itself* but not their arithmetic; #46 owns first-frame content size. Partial.
- **Proposed title:** `feat(renderer): contentInset and contentOffset — the inset area scrolls and hit-tests, and scrollToEnd lands on the adjusted content end`
- **Acceptance:** unit (100 % gate) — clamp and end-target arithmetic over insets on all four edges; e2e — a wheel
  event delivered inside the top inset scrolls; `contentOffset` on mount is honoured on frame one; golden — a
  ScrollView with `contentInset` and an indicator.

### A4. `scrollToIndex`, `scrollToEnd` and `initialScrollIndex` land on the item

- [web#1854](https://github.com/necolas/react-native-web/issues/1854) — open, +17, c4: `scrollToIndex` and
  `scrollToLocation` do nothing on FlatList/SectionList.
- [core#54643](https://github.com/react/react-native/issues/54643) — closed, +1, c7: `scrollToIndex`,
  `scrollToOffset`, `scrollToItem`, `scrollToEnd` all fail.
- [core#43587](https://github.com/react/react-native/issues/43587) — closed, +2, c7: `scrollToIndex` mis-positions
  an index that is in `stickyHeaderIndices`.
- [core#33149](https://github.com/react/react-native/issues/33149) — closed, c1: `scrollToIndex` does not trigger
  `onViewableItemsChanged`.
- [core#39618](https://github.com/react/react-native/issues/39618) — closed: wrong position.

**Why it applies.** `VirtualizedList` computes the target from `getItemLayout` or from measured frames and then
issues one `scrollTo`; it can only be right if our `onLayout` cadence, the sticky-header offset and the
`onScroll` payload after a programmatic scroll all match what the JS assumes. This is an end-to-end test of three
contracts we already promise separately; it is cheap and it is exactly what web got wrong.

- **Epic:** #209 (test-suite parity) — it is a conformance test over the unmodified `VirtualizedList`.
- **DEDUPE:** #109 covers `scrollTo` not re-emitting; #45 covers the `onScroll` cadence; #98 covers touchables in
  sticky subtrees. None asserts where a `scrollToIndex` lands.
- **Proposed title:** `test(renderer): scrollToIndex, scrollToEnd and initialScrollIndex land on the item — with sticky headers, late-measured cells and onViewableItemsChanged`
- **Acceptance:** e2e — a 500-row FlatList bundle: `scrollToIndex(k)` for k in a table puts row k's top at the
  viewport top (± 0.5 px) and fires `onViewableItemsChanged` with k first; `initialScrollIndex` holds on frame
  one; unit (100 % gate) — the sticky-header offset arithmetic.

### A5. Inverted lists

- [core#54181](https://github.com/react/react-native/issues/54181) — open, +10, c19: `inverted` flips shadows on
  iOS 26 — because `inverted` is a `scaleY: -1` on the container *and* on every cell.
- [core#34583](https://github.com/react/react-native/issues/34583) — closed, +18, c17: app freezes with animations
  inside an inverted FlatList.
- [core#35341](https://github.com/react/react-native/issues/35341) — closed, +6, c3: crash/poor performance with
  RTL + inverted; [core#35983](https://github.com/react/react-native/issues/35983) — closed, +3, c16: frame drops;
  [core#44151](https://github.com/react/react-native/issues/44151) — closed, +3, c6.
- [web#995](https://github.com/necolas/react-native-web/issues/995) — closed, +11, c20: wheel direction is
  inverted with the container; [web#1807](https://github.com/necolas/react-native-web/issues/1807) — closed, +7:
  copy/paste comes out backwards; [web#1579](https://github.com/necolas/react-native-web/issues/1579) — closed,
  c15: rendering problem under specific conditions.
- [rnw#12763](https://github.com/microsoft/react-native-windows/issues/12763) — open, c3: inverted horizontal
  FlatList displays incorrectly on Fabric.

**Why it applies.** `inverted` is not a list feature; it is a pair of transforms, and every platform that got
transforms slightly wrong (hit-testing, wheel sign, shadow direction, text selection order, damage rectangles)
found out through this prop. We own transforms, hit-testing and wheel input, so we own every one of those failure
modes at once. Chat UIs in the ecosystem use it universally.

- **Epic:** #173 (M1).
- **DEDUPE:** #45 cites rn-macos#1421 (inverted list mis-handles keyboard) for cadence only; #35/#103 cover
  transform hit-testing generally; #119 covers RTL scroll origin. No issue exercises `inverted` end to end.
- **Proposed title:** `test(renderer): inverted lists — scaleY(-1) on container and cells, wheel direction, hit-testing, sticky headers and selection order`
- **Acceptance:** golden — an inverted list with a `boxShadow` cell and a sticky header; e2e — a wheel-down on an
  inverted list moves toward newer rows, a press lands on the painted row, selection copies in reading order;
  perf — the frame-cost gate (#124) runs the fixture inverted and non-inverted with equal budgets.

### A6. A scroll that starts during a press cancels it

- [core#48488](https://github.com/react/react-native/issues/48488) — open, +3, c3: `onPress` fires on FlatList rows
  while scrolling a list that fits the screen.
- [core#44610](https://github.com/react/react-native/issues/44610) — closed, +1, c16: `Pressable` gets stuck in the
  pressed state under the new architecture.
- [web#731](https://github.com/necolas/react-native-web/issues/731) — closed, +10, c12: `onPress` triggered when
  scrolling at the top of a ScrollView; [web#1534](https://github.com/necolas/react-native-web/issues/1534) —
  closed, +6, c7: Touchable should not be pressed while scrolling.
- [rnw#4614](https://github.com/microsoft/react-native-windows/issues/4614) — open, c2: a list item remains
  pressed after the scroll completes.

**Why it applies.** The responder system cancels a press when the scroll responder takes over — on touch. On a
desktop the same sequence is: button down on a row, wheel or touchpad scroll, button up. There is no responder
hand-off for wheel input in React Native at all, so the platform must synthesize the cancel (`onPressOut` without
`onPress`, `onResponderTerminate`) and clear the pressed state, or every list on Linux has
[rnw#4614](https://github.com/microsoft/react-native-windows/issues/4614).

- **Epic:** #173 (M1) — `Pressable` and `ScrollView` are both M1.
- **DEDUPE:** #215 runs the upstream Pressability contracts unmodified (touch-shaped); #98 covers touchables inside
  scrolled subtrees staying touchable, the opposite direction. Wheel-during-press is nobody's.
- **Proposed title:** `test(input): a scroll that starts during a press cancels it — no onPress after the offset moves, no stuck pressed state`
- **Acceptance:** unit (100 % gate) — the cancel path in the input pipeline for wheel, touchpad and touch; e2e —
  press-down, inject a wheel event, release: event trace shows `pressIn`, `pressOut`, no `press`, and the row's
  pressed style is gone in the next golden frame.

### A7. `RefreshControl` on Linux — a decision

- [core#53987](https://github.com/react/react-native/issues/53987) — open, +25, c29: `tintColor` ignored, indicator
  stuck after navigation.
- [core#35779](https://github.com/react/react-native/issues/35779) — open, +24, c49: indicator not shown on iOS.
- [core#56343](https://github.com/react/react-native/issues/56343) — open, +5, c8: props not applied on initial
  mount on Fabric; [core#51914](https://github.com/react/react-native/issues/51914) — closed, +3, c17: `style`
  ignored.
- [web#1027](https://github.com/necolas/react-native-web/issues/1027) — open, +25, c9: never implemented on web.
- [rnw#13806](https://github.com/microsoft/react-native-windows/issues/13806) — open: `refreshControl` not
  implemented for Fabric.

**Why it applies.** Three desktop-ish platforms (web, Windows Fabric, and — per the issue counts — iOS itself)
have no working `RefreshControl`, and it is one of the most-reacted component clusters upstream. On Wayland the
only natural trigger is touchpad overscroll (no `wl_pointer` "pull"); on a touchscreen it is a real gesture. This
is a decide-then-do item, not a feature: draw the indicator and fire `onRefresh` from an overscroll gesture, or
mount it as a no-op that never fires and say so in the support matrix (#87).

- **Epic:** #176 (M4) — components beyond the six, flagship-driven; `needs:decision`.
- **DEDUPE:** no open issue mentions `RefreshControl`. #87 (support matrix) is where the answer is recorded.
- **Proposed title:** `decide RefreshControl on Linux — touchpad overscroll pull with a drawn indicator, or an explicit no-op in the support matrix`
- **Acceptance:** the decision recorded in #87; if implemented — golden of the indicator at three pull distances,
  e2e of a touchpad overscroll firing `onRefresh` once, unit of the pull-distance state machine in the gate.

---

## Group B — Input: coordinates, mid-gesture unmounts, and focus visibility

### B1. Pointer-event coordinate spaces

- [core#53366](https://github.com/react/react-native/issues/53366) — open, +1, c8: `locationX`/`locationY` on
  Android Fabric are inconsistent — `locationY` equals `pageY` on some `onPressOut` paths.
- [core#39282](https://github.com/react/react-native/issues/39282) — closed, c2: `locationX`/`Y` wrong on repeated
  presses.
- [core#50465](https://github.com/react/react-native/issues/50465) — closed, +1, c9: `TextInput` and `Pressable`
  with `opacity: 0` receive no touches on iOS (they must: opacity is paint, not hit-testing).

**Why it applies.** Every event we emit carries four coordinate pairs — `locationX/Y` (target box), `pageX/Y`
(surface), and the pointer-event `offsetX/Y`, `clientX/Y` — and under a transformed or scrolled ancestor they
differ. Nothing in the tracker asserts the payload, only the *target*. The `opacity: 0` case is the same class:
a paint property must not leak into the hit test.

- **Epic:** #173 (M1).
- **DEDUPE:** #35 and #235 assert the hit *target*; #115 and #215 mention `pageX` in passing; #64 owns
  `pointerEvents`; #105 owns opacity as a group *paint* property. The payload numbers are unowned.
- **Proposed title:** `test(input): pointer-event coordinate spaces — locationX/Y in the target box, pageX/Y in the surface, under transforms, scroll and opacity:0 targets`
- **Acceptance:** unit (100 % gate) — payload construction over a table of (transform, scroll offset, hitSlop,
  fractional scale); e2e — an injected pointer at a known surface point yields the expected four pairs on a nested,
  rotated, scrolled `Pressable`, and a `Pressable` at `opacity: 0` still presses.

### B2. The hovered, pressed or focus-target node unmounts mid-gesture

- [core#55489](https://github.com/react/react-native/issues/55489) — open, 2026-02, c2: iOS `RCTAssert` crash when
  unmounting near the cursor's hover effect.
- [core#57951](https://github.com/react/react-native/issues/57951) — open, 2026-08, +1, c2: null-pointer crash in
  `FabricUIManager::findNextFocusableElement` when focusing a `TextInput` inside a ScrollView.
- [core#33021](https://github.com/react/react-native/issues/33021) — closed, +2, c3: unrecoverable crash calling
  `setAccessibilityFocus` on a component that is unmounting.

**Why it applies.** Three platforms crashed on the same shape: a gesture or traversal holds a node the mounting
layer is deleting on another thread. Our pipeline holds hover, press, focus and accessibility-focus targets, and
the `Delete` mutation is applied on the UI thread while pointer events arrive from the Wayland thread — this is a
TSan case by construction, and AGENTS.md rule 6 says it needs a TSan-clean test.

- **Epic:** #173 (M1).
- **DEDUPE:** #37 states "when the focused node unmounts, focus moves to the nearest still-mounted ancestor";
  #36 (hover chain) and #107 (scene-node reuse) do not cover an unmount mid-hover or mid-press.
- **Proposed title:** `test(input): a node unmounts while hovered, pressed or being focused — leave and cancel events fire, nothing dangles, TSan-clean`
- **Acceptance:** unit (100 % gate, under TSan) — delete the hovered/pressed/focus-target node from a second thread
  while events are in flight; e2e — the event trace shows `pointerLeave`/`pressOut` for the vanished node and the
  next hit goes to what is now under the pointer.

### B3. Focus-visible, `focus()` options and scroll-into-view

- [web#1849](https://github.com/necolas/react-native-web/issues/1849) — open, c7: `Pressable` needs
  `focus-visible` — a ring for keyboard focus, none for pointer focus.
- [web#2491](https://github.com/necolas/react-native-web/issues/2491) — open, +4, c4: `.focus()` should take
  `preventScroll` / `focusVisible`.
- [web#2823](https://github.com/necolas/react-native-web/issues/2823) — open, 2026-01: Shift+Tab into a ScrollView
  does not reach focusable items (the target is not scrolled into view).
- [web#2605](https://github.com/necolas/react-native-web/issues/2605) — closed, +5, c4: `onPress` fires twice via
  Enter when disabled; [web#2560](https://github.com/necolas/react-native-web/issues/2560) — closed, c4: no
  `onPress` on Space for `role="button"`; [web#2681](https://github.com/necolas/react-native-web/issues/2681) —
  open: no `onPress` on Enter for `role="link"`; [web#2757](https://github.com/necolas/react-native-web/issues/2757)
  — closed: a disabled `Pressable` gets a focused state when its children are focused.
- [core#34023](https://github.com/react/react-native/issues/34023) — closed, c10: `onLongPress` does not fire from
  a physical keyboard.

**Why it applies.** #37 builds the focus model; this is the second layer every desktop toolkit added after
shipping the first: the ring only for keyboard-initiated focus, `focus()` moving the scroll offset so the node is
visible (and `preventScroll` to stop it), Tab into a scrolled container revealing the target, and activation
firing *exactly once* per key with the role deciding whether Enter, Space or both activate. Web collected these
one at a time over five years; we can take them as one contract.

- **Epic:** #173 (M1).
- **DEDUPE:** #37 owns the ring, Tab order, Enter/Space and `disabled`-implies-not-focusable. It does not
  distinguish keyboard from pointer focus, does not define `focus()` scrolling, and does not say "exactly once".
  #91 gates traversal order, not visibility. Partial.
- **Proposed title:** `feat(input): focus-visible, focus() options and scroll-into-view — the ring paints for keyboard focus only, Tab into a ScrollView reveals the target, activation fires exactly once per role`
- **Acceptance:** unit (100 % gate) — focus-source tracking and the role→activation-key table; golden — same node
  focused by pointer (no ring) and by Tab (ring); e2e — Shift+Tab into a scrolled list reveals and focuses the
  last row; Enter on a disabled `Pressable` emits nothing.

---

## Group C — Text: what shaping does with the props the six components already accept

### C1. Colour emoji and fontconfig fallback

- [core#56183](https://github.com/react/react-native/issues/56183) — open, +9, c11: emoji render as `[?]` boxes
  on the iOS 26 simulator (a fallback-font lookup failure).
- [core#47621](https://github.com/react/react-native/issues/47621) — open, +2, c14: emoji inside `<Text>` increase
  the line height or stretch the element.
- [core#57995](https://github.com/react/react-native/issues/57995) — open, 2026-08: the last emoji is cut off when
  the text contains U+1FAE8; [core#44929](https://github.com/react/react-native/issues/44929) — closed: emoji
  clipped under some `fontFamily`/`fontSize`/`lineHeight` combinations.
- [core#48625](https://github.com/react/react-native/issues/48625) — open, c5: a fallback list in `fontFamily` is
  not supported.
- [rnw#3918](https://github.com/microsoft/react-native-windows/issues/3918) — open, c2: handle an incorrect
  `fontFamily` gracefully; [rnw#3816](https://github.com/microsoft/react-native-windows/issues/3816) — open: a
  font-registration system.

**Why it applies.** ADR-0001 chose SkParagraph over fontconfig; emoji is where that choice is tested first, because
Noto Color Emoji on Linux is a CBDT (bitmap) font, newer builds are COLRv1, and Skia's FreeType port handles the
two differently. The fallback run for an emoji has its own ascent/descent, and
[core#47621](https://github.com/react/react-native/issues/47621) is what happens when the line box takes it. Our
prior-art report calls text-and-emoji a cross-cutting killer and no issue owns it.

- **Epic:** #173 (M1).
- **DEDUPE:** #70 owns bundled-asset registration and failing loudly on an unknown family; #111 mentions "same
  fallback fonts" as an input to measure/paint equality; #110 owns vertical metrics. None names emoji, colour
  glyph formats, or the fontconfig fallback chain.
- **Proposed title:** `feat(text): colour emoji and fontconfig fallback — COLR and CBDT glyphs rasterize, a fallback run does not inflate the line box, and a missing family resolves deterministically`
- **Acceptance:** golden — a line mixing Latin, an emoji, a ZWJ sequence and CJK at three `lineHeight` values,
  rasterized with a pinned Noto Color Emoji; unit (100 % gate) — the fallback resolution order for a family list
  and for an unknown family; e2e — `onTextLayout` line height equals the golden's under an emoji.

### C2. The text-style matrix: `letterSpacing`, `textTransform`, `textDecorationLine`, `textShadow`, `fontVariant`

- [core#46436](https://github.com/react/react-native/issues/46436) — open, +10, c5: an extra line wrap from a
  combination of `lineHeight`, `letterSpacing`, `maxFontSizeMultiplier` and `fontFamily` — measurement and shaping
  disagree once spacing is involved.
- [core#54823](https://github.com/react/react-native/issues/54823) — open, +1, c7: `letterSpacing` wrong;
  [core#37511](https://github.com/react/react-native/issues/37511) — closed, +2, c7: `ellipsizeMode="middle"` with
  `letterSpacing`.
- [core#39524](https://github.com/react/react-native/issues/39524) — closed, +8, c5: `textTransform` does nothing
  on Android New Architecture; [core#34117](https://github.com/react/react-native/issues/34117) — open, +2, c6:
  `capitalize` does not match CSS; [core#38499](https://github.com/react/react-native/issues/38499) — closed, c11.
- [rnw#14380](https://github.com/microsoft/react-native-windows/issues/14380) — open, c3: multiline `TextInput`
  loses `fontFamily`/`letterSpacing`.

**Why it applies.** Each of these props is a shaping input (`letterSpacing`, `fontVariant` → OpenType features
such as `tnum`), a pre-shaping string transform (`textTransform`, which changes the string *length* and therefore
the wrap), or a post-shaping decoration (`textDecorationLine`, `textShadow`) — and the measure path and the paint
path must apply the same ones or #111's measure/paint equality holds for plain text only. `fontVariant:
['tabular-nums']` is what keeps any timer or score display from jittering, which matters to a game flagship.

- **Epic:** #173 (M1).
- **DEDUPE:** #111 lists `letterSpacing` as a cache-invalidation trigger; #70 mentions `fontVariant` once
  (registration); #14 is the pipeline. There is no golden matrix over these props.
- **Proposed title:** `test(text): letterSpacing, textTransform, textDecorationLine, textShadow and fontVariant enter shaping and measurement identically — golden matrix`
- **Acceptance:** golden — one fixture per prop value including `tabular-nums` over a changing digit string and
  `capitalize` on a hyphenated word; unit (100 % gate) — measured width equals painted width for every row of
  the matrix, and `textTransform` is applied before wrap.

### C3. `ellipsizeMode` head, middle, tail and clip

- [web#1336](https://github.com/necolas/react-native-web/issues/1336) — open, +31, c3: `ellipsizeMode` does not
  work on web (only `tail` is expressible in CSS).
- [core#37511](https://github.com/react/react-native/issues/37511) — closed, +2, c7: `middle` breaks with
  `letterSpacing`.

**Why it applies.** #111's own context says "the ellipsis is always at the tail" on our side. `head`, `middle` and
`clip` are a search over the shaped glyph runs for the widest prefix/suffix pair that fits, with the ellipsis
carrying the truncation's own style ([core#37926](https://github.com/react/react-native/issues/37926), cited in
#111). Web never implemented it because CSS cannot; SkParagraph can, per run.

- **Epic:** #173 (M1).
- **DEDUPE:** #111 records the gap in prose but its acceptance criteria only test truncation as such. Partial;
  this is the implementing sibling.
- **Proposed title:** `feat(text): ellipsizeMode head, middle and clip — a per-run truncation search, with the ellipsis styled by the truncation`
- **Acceptance:** golden — the four modes at `numberOfLines` 1 and 2, with `letterSpacing` and a nested bold
  fragment; unit (100 % gate) — the fit search over synthetic run widths.

### C4. `adjustsFontSizeToFit` and `minimumFontScale`

- [rnw#13829](https://github.com/microsoft/react-native-windows/issues/13829) — open, c3: implement
  `minimumFontScale` for Fabric.
- [core#52642](https://github.com/react/react-native/issues/52642) — open, +17 (already cited by #111, which
  records that we *ignore* the prop).

**Why it applies.** A shrink loop over the paragraph until it fits `numberOfLines`, bounded by
`minimumFontScale`; on a canvas it is three lines around the layout call plus a cache key that includes the
resolved scale, otherwise the measure cache returns the unshrunk size and the box is wrong.

- **Epic:** #173 (M1). Priority P2.
- **DEDUPE:** #111 and #113 name the prop as ignored; neither has an acceptance criterion for it.
- **Proposed title:** `feat(text): adjustsFontSizeToFit and minimumFontScale — the shrink loop, its cache key, and a golden`
- **Acceptance:** golden — a long string in a fixed box at `minimumFontScale` 0.5 and 1.0; unit (100 % gate) —
  the loop terminates and the measure cache is keyed on the resolved scale.

### C5. `textAlign: start/end` and `justify` line boxes

- [core#45255](https://github.com/react/react-native/issues/45255) — closed, +7, c13: add `textAlign: start/end`
  (landed).
- [core#50859](https://github.com/react/react-native/issues/50859), [core#50226](https://github.com/react/react-native/issues/50226)
  — closed, c4 each: `onTextLayout` reports wrong lines under `textAlign: 'justify'`.

**Why it applies.** `start`/`end` resolve against the paragraph direction, which #72 makes a runtime value; and
`justify` changes glyph positions after line breaking, so the line boxes `onTextLayout` reports must come from
the justified layout, not the pre-justification one.

- **Epic:** #173 (M1). Priority P2.
- **DEDUPE:** #72/#119 own direction and logical edges; #111 owns `onTextLayout`. The alignment values are not
  enumerated anywhere.
- **Proposed title:** `test(text): textAlign start/end resolve against paragraph direction, and justify reports true line boxes in onTextLayout`
- **Acceptance:** golden — the six alignment values in LTR and RTL; e2e — `onTextLayout` under `justify` matches
  the golden's glyph extents.

### C6. `selectable` is paint and input, not layout

- [core#48921](https://github.com/react/react-native/issues/48921) — open, +5, c25: `selectable` breaks truncation
  and `lineHeight` on Android.
- [core#55187](https://github.com/react/react-native/issues/55187) — open, +6, c3; [core#54686](https://github.com/react/react-native/issues/54686)
  — closed, +3, c6: `selectable` stops working on iOS 26.
- [core#33494](https://github.com/react/react-native/issues/33494) — closed, +9, c9: the selected area is not
  visible; [core#33419](https://github.com/react/react-native/issues/33419) — open, c10: tapping selectable text
  inside a ScrollView jumps to the top.
- [rnw#15481](https://github.com/microsoft/react-native-windows/issues/15481) — open, 2025-12: text selection
  outstanding tasks; [rnw#13113](https://github.com/microsoft/react-native-windows/issues/13113) — open:
  `selectionColor` for Fabric.

**Why it applies.** Android swaps the text widget for a selectable one and the layout changes; we have one
paragraph and selection is a highlight plus a drag responder, so the *test* is that turning `selectable` on
changes no measured size, no truncation, no scroll offset — and that the selection is published to the Wayland
primary selection, which is the desktop-Linux expectation for any selected text.

- **Epic:** #173 (M1). Priority P2.
- **DEDUPE:** #43 owns selectable text as a feature; #60 owns `wl_data_device` and primary selection. The
  layout-invariance assertion and `selectionColor` are new.
- **Proposed title:** `test(text): selectable changes no layout — truncation, lineHeight and scroll offset are invariant, selectionColor paints, and a selection reaches the primary-selection clipboard`
- **Acceptance:** unit (100 % gate) — measured size equality with `selectable` on/off over #111's table; golden —
  a selection highlight with a custom `selectionColor` inside a truncated line; e2e — drag-select then middle-click
  paste into a `TextInput` via the harness's `wl_data_device`.

### C7. `TextInput` placeholder

- [core#50137](https://github.com/react/react-native/issues/50137) — open, +2, c18: the placeholder ignores the
  custom `fontFamily`/weight; [core#45853](https://github.com/react/react-native/issues/45853) — open, c9: same on
  Android 14.
- [core#42589](https://github.com/react/react-native/issues/42589) — closed, c19: placeholder ignores
  `letterSpacing`; [core#41241](https://github.com/react/react-native/issues/41241) — closed, c7: does not scale
  with the font; [core#41105](https://github.com/react/react-native/issues/41105) — closed, +1, c5: caret misaligned
  when the value is empty and a placeholder is shown; [core#38528](https://github.com/react/react-native/issues/38528)
  — closed: wrong caret alignment with `textAlign="center"`.
- [core#44230](https://github.com/react/react-native/issues/44230) — open, c5: VoiceOver reads the placeholder;
  [core#55004](https://github.com/react/react-native/issues/55004) — closed, +2, c12: the placeholder overrides
  `accessibilityLabel`.

**Why it applies.** The placeholder is a second paragraph painted with the *input's* style at the position the
first glyph of the value would take; the caret for an empty value sits at that same position under every
`textAlign`. Every native widget gets one of those two wrong. In the AT-SPI tree the placeholder is the
`placeholder-text` attribute, never the name.

- **Epic:** #173 (M1).
- **DEDUPE:** #53 (TextInput parity matrix) covers controlled value, multiline, selection, paste,
  `secureTextEntry` — not the placeholder; #37's "placeholder" hit is unrelated wording; #17 owns the caret.
- **Proposed title:** `test(text): the placeholder is painted with the input's own font, weight, letterSpacing and alignment, the caret sits where the first glyph would, and the placeholder is a hint, not the name`
- **Acceptance:** golden — empty input with placeholder at left/center/right alignment and a custom font, caret
  visible; unit (100 % gate) — caret x for an empty value equals the placeholder's first-glyph x; e2e — the
  AT-SPI tree dump (#216) shows `accessibilityLabel` as name and the placeholder as an attribute.

### C8. Multiline `TextInput` inner scrolling and caret-follow

- [core#49226](https://github.com/react/react-native/issues/49226) — open, +6, c9: a multiline input inside a
  ScrollView scrolls to its top when typing overflows to a new line.
- [core#35717](https://github.com/react/react-native/issues/35717) — closed, +8, c9: screen does not scroll to a
  multiline input on focus; [core#39660](https://github.com/react/react-native/issues/39660) — closed, +5, c13:
  the scroll position jumps on a new line; [core#35388](https://github.com/react/react-native/issues/35388) —
  closed, +6, c22: no scrolling with `multiline` and `editable={false}`.
- [rnw#14379](https://github.com/microsoft/react-native-windows/issues/14379) — open: overflowed text is
  unreachable and the height grows wrongly; [rnw#15166](https://github.com/microsoft/react-native-windows/issues/15166)
  — open, +2, c3: space for the next line is reserved before wrapping happens.
- [rn-macos#925](https://github.com/microsoft/react-native-macos/issues/925) — open, c4: `multiline` with
  `scrollEnabled={false}` does not work.

**Why it applies.** A multiline input is a ScrollView with a paragraph and a caret in it. On the canvas we write
the inner scroll ourselves: the caret line must stay visible after every edit, `scrollEnabled={false}` means the
box grows instead, `editable={false}` still scrolls, and the *enclosing* ScrollView must reveal the caret line
without fighting the inner one — the exact tug-of-war in [core#49226](https://github.com/react/react-native/issues/49226).

- **Epic:** #173 (M1).
- **DEDUPE:** #114 owns content size and auto-grow; #17 owns the caret and editing model; #53 the parity matrix.
  Inner scrolling and caret-follow are unowned; rn-macos#2905 (already cited) is the same bug on macOS.
- **Proposed title:** `feat(text): multiline TextInput inner scrolling — the caret stays visible after every edit, scrollEnabled=false grows instead, and the enclosing ScrollView reveals the caret line without a tug-of-war`
- **Acceptance:** e2e — type 40 lines into a 5-line input inside a ScrollView: the caret line is inside both
  viewports after every keystroke, and the outer offset moves monotonically; golden — the inner scroll at
  top/middle/bottom; unit (100 % gate) — the caret-follow arithmetic and the `scrollEnabled`/`editable` table.

---

## Group D — Images

### D1. Animated GIF and WebP on the frame clock

- [core#33039](https://github.com/react/react-native/issues/33039) — closed, +12, c15: GIFs sped up on 120 Hz
  devices — frame durations tied to the display rate instead of the file.
- [core#46095](https://github.com/react/react-native/issues/46095) — closed, +7, c8; [core#47408](https://github.com/react/react-native/issues/47408)
  — closed, +1, c14: GIFs stop working after a minor bump.
- [core#42132](https://github.com/react/react-native/issues/42132) — open, +2, c6: a GIF with `resizeMode="repeat"`
  renders incorrectly; [core#39792](https://github.com/react/react-native/issues/39792) — open: `blurRadius` not
  applied to GIFs; [core#46810](https://github.com/react/react-native/issues/46810) — closed, c4: switching the
  source between PNG and GIF.

**Why it applies.** SkCodec decodes GIF and animated WebP frames with per-frame durations; the platform schedules
the next frame on its frame clock. [core#33039](https://github.com/react/react-native/issues/33039) is the exact
bug a 120 Hz-first platform will write if the schedule is "next vsync" instead of "duration elapsed", and every
per-frame image must go through the same `resizeMode`/`blurRadius` path as a static one or the two open issues
above reappear. Damage tracking must mark only the image's rectangle, and a clipped-out animation must not run.

- **Epic:** #173 (M1) for decode and props; the pacing is on the frame clock #59 delivers in M2.
- **DEDUPE:** #15 (decode pipeline) does not mention animation; #44's single "gif" hit is a source-format aside;
  #108 owns bitmap lifetime. Unowned.
- **Proposed title:** `feat(renderer): animated GIF and WebP — SkCodec frame durations on the frame clock, paused when clipped out, resizeMode and blurRadius applied per frame`
- **Acceptance:** unit (100 % gate) — frame scheduling from durations at 60 and 120 Hz produces the same
  wall-clock sequence; golden — frame 3 of a fixture GIF under `repeat` and under `blurRadius`; perf — the
  damage region for a looping GIF equals its box (#12).

### D2. The `Image` props the resizeMode matrix does not reach

- [rnw#13117](https://github.com/microsoft/react-native-windows/issues/13117) — open, c1: `capInsets`;
  [rnw#13747](https://github.com/microsoft/react-native-windows/issues/13747) — open: `defaultSource`;
  [rnw#13749](https://github.com/microsoft/react-native-windows/issues/13749) — open: `loadingIndicatorSource`;
  [rnw#14542](https://github.com/microsoft/react-native-windows/issues/14542) — open: `objectFit`;
  [rnw#13165](https://github.com/microsoft/react-native-windows/issues/13165) — open, c3: `overflow` on Image;
  [rnw#12578](https://github.com/microsoft/react-native-windows/issues/12578) — open: `source={{}}` versus
  `uri: undefined` behave differently; [rnw#6571](https://github.com/microsoft/react-native-windows/issues/6571)
  — open: `Image.prefetchWithMetadata`.
- [core#54120](https://github.com/react/react-native/issues/54120) — open, +3, c6: `onLoadStart`/`onLoad`/`onLoadEnd`
  fire out of order; [core#45188](https://github.com/react/react-native/issues/45188) — closed, c4: `onLoad`
  payload differs between platforms.
- [web#1019](https://github.com/necolas/react-native-web/issues/1019) — open, +4, c4: HTTP headers on `source`.

**Why it applies.** react-native-windows tracked each missing `Image` prop as its own issue after shipping; that
is the backlog shape #69 (prop-coverage conformance) exists to prevent. `capInsets` is a nine-slice draw
(`SkCanvas::drawImageNine`), `defaultSource` is a second decode, `objectFit` is an alias table, and the load
event order and payload are a contract the flagship's image-heavy screens will read.

- **Epic:** #173 (M1). Priority P2.
- **DEDUPE:** #44 owns `resizeMode` and the load lifecycle *events existing*; #69 owns prop coverage
  generically; #15 owns http sources. The named props and the event order/payload are new.
- **Proposed title:** `feat(renderer): Image capInsets, defaultSource, objectFit and source headers — with the onLoadStart/onLoad/onLoadEnd order and payload pinned`
- **Acceptance:** golden — a nine-slice button at two sizes, and `defaultSource` visible before a slow decode;
  unit (100 % gate) — `objectFit`→`resizeMode` table and the event order under success, failure and source swap;
  e2e — a source with a custom header reaches the harness's HTTP stub with that header.

---

## Group E — Renderer and modules: aliases, appearance, the next two components, and dialogs

### E1. Web-prop aliases resolve identically

- [core#34424](https://github.com/react/react-native/issues/34424) — closed, +40, c26: the W3C "Web Props" umbrella
  — `role`, `aria-*`, `tabIndex`, `id`, and the CSS-alias style props.
- [rnw#11185](https://github.com/microsoft/react-native-windows/issues/11185) — open, c3: web-prop parity in
  Fabric; [rnw#11184](https://github.com/microsoft/react-native-windows/issues/11184) — open, c3: web *style*
  prop parity; [rnw#11905](https://github.com/microsoft/react-native-windows/issues/11905) — open: aria props on
  Fabric; [rnw#14569](https://github.com/microsoft/react-native-windows/issues/14569) — open: `aria-labelledby`;
  [rnw#12513](https://github.com/microsoft/react-native-windows/issues/12513)/[rnw#12514](https://github.com/microsoft/react-native-windows/issues/12514)
  — open: `accessibilityLabeledBy` and `nativeID` for labelled-by.

**Why it applies.** The alias resolution lives partly in JS (`processAriaProps`, `StyleSheet`) and partly in the
C++ `ViewProps`/`AccessibilityProps` parsers we vendor. Because we take a platform fork of the JS layer (ADR-0001
decision 9), an alias that resolves on iOS can silently stop resolving on Linux after an override. The test is
mechanical: every alias in the upstream type produces the same shadow-node value as its canonical name.

- **Epic:** #209 (test-suite parity). Priority P2.
- **DEDUPE:** #69 covers "every declared prop asserted" but per component, not alias-versus-canonical; #61
  covers roles. The equality assertion is new.
- **Proposed title:** `test(renderer): the web-prop aliases — role, aria-*, tabIndex, id and the CSS-alias style props resolve to the same shadow-node values as their React Native names`
- **Acceptance:** unit (100 % gate, Fantom-style runner #210) — a generated table of alias/canonical pairs from
  the upstream type files, mounted and compared; e2e — `aria-labelledby` appears as an AT-SPI `labelled-by`
  relation in the #216 tree dump.

### E2. `Appearance.setColorScheme` overrides the system theme

- [web#2703](https://github.com/necolas/react-native-web/issues/2703) — open, +19, c9: `Appearance.setColorScheme`
  does nothing on web.
- [rnw#13279](https://github.com/microsoft/react-native-windows/issues/13279) — open, c2: `PlatformColor` does not
  respect `Appearance.setColorScheme`.
- [core#48493](https://github.com/react/react-native/issues/48493) — open, +7, c8: dev app crashes when switching
  the colour scheme while a `Text` uses `DynamicColorIOS`.

**Why it applies.** #52 makes a portal theme change invalidate every resolved colour. `setColorScheme` is the
app-level override of that same input, and the two desktop platforms that shipped `PlatformColor` both forgot to
route it through the same invalidation. The crash in [core#48493](https://github.com/react/react-native/issues/48493)
is the mid-frame variant: a colour resolved on the render thread while the scheme changes on the JS thread.

- **Epic:** #175 (M3), beside #52 and #23. Priority P2.
- **DEDUPE:** #52 owns the OS-theme path; `setColorScheme` appears in no issue body. Partial.
- **Proposed title:** `test(modules): Appearance.setColorScheme overrides the portal theme, re-resolves every PlatformColor, and a switch mid-frame never tears`
- **Acceptance:** unit (100 % gate) — override precedence table (`null` restores the portal value); golden — the
  same screen under `light`, `dark` and `null` with a portal set to dark; TSan — a scheme switch racing a frame.

### E3. `Switch` and `ActivityIndicator` drawn on the canvas

- [core#36564](https://github.com/react/react-native/issues/36564) — closed, +5, c10: `ActivityIndicator` a few
  pixels off centre on iOS.
- [core#49056](https://github.com/react/react-native/issues/49056) — closed, +9, c33: `ActivityIndicator` ref
  forwarding.
- [rn-macos#1699](https://github.com/microsoft/react-native-macos/issues/1699) — open: Fabric `Switch` does not
  render; [rn-macos#1700](https://github.com/microsoft/react-native-macos/issues/1700) — open: `ActivityIndicator`
  Fabric shim; [rnw#13108](https://github.com/microsoft/react-native-windows/issues/13108) — open, c2: `overflow`
  on `Switch`.

**Why it applies.** They are the seventh and eighth components every RN app uses, lucid-softworks ships both, and
on a canvas each is a few draw calls plus a frame-clock animation (`Switch` thumb, indicator rotation) — the same
mechanism D1 and #19 need. Landing them proves the "component beyond the six" path before the flagship's list
(#87) forces it.

- **Epic:** #176 (M4). Priority P2.
- **DEDUPE:** the only `Switch`/`ActivityIndicator` hits in our tracker are unrelated words in #52, #162 and #210.
- **Proposed title:** `feat(renderer): Switch and ActivityIndicator drawn on the canvas — track, thumb and spinner on the frame clock, pinned by golden`
- **Acceptance:** golden — `Switch` on/off/disabled with `trackColor`/`thumbColor`, indicator at `small`/`large`
  with a fixed clock phase; e2e — a press toggles and fires `onValueChange` once; unit (100 % gate) — geometry.

### E4. `Alert`

- [web#1026](https://github.com/necolas/react-native-web/issues/1026) — open, +43, c38: `Alert` never implemented
  on web — the single most-reacted open issue in that tracker.

**Why it applies.** `Alert` is core API with no Linux backend in #23's module list, and on a bare Wayland surface
there is no system dialog to call; it is either a drawn in-surface dialog (focus trap, Escape cancels, Enter
activates the default button, an AT-SPI `dialog` role) or an `xdg_popup`/portal dialog — the same decision #62
faces for `Modal`. Web's 43 reactions say apps do call it.

- **Epic:** #175 (M3), beside #23. Priority P2, `needs:decision` shared with #62.
- **DEDUPE:** #23 lists Appearance, Dimensions, Clipboard, Linking, storage — no `Alert`; #62 decides overlay
  surfaces; #83 mentions `Alert` only as a localization example (rnw#3742). Unowned.
- **Proposed title:** `feat(modules): Alert — a drawn in-surface dialog with focus trapping, Escape/Enter mapping and an AT-SPI dialog role, on the overlay surface #62 decides`
- **Acceptance:** golden — two-button and three-button alerts; e2e — Tab cycles inside the dialog only, Escape
  fires the cancel button, Enter the default; unit (100 % gate) — button-order and default-button table.

---

## Group F — Core: timing

### F1. `requestAnimationFrame` is FIFO and never starves

- [core#48005](https://github.com/react/react-native/issues/48005) — closed, +2, c8: rAF callback order is
  nondeterministic (deterministic per spec and on web).
- [core#57592](https://github.com/react/react-native/issues/57592) — closed, +2, c3, 2026-07: on bridgeless iOS,
  `setTimeout`, rAF and native→JS callbacks stall for seconds while idle until a touch un-sticks them.
- [core#47888](https://github.com/react/react-native/issues/47888) — closed, c11: rAF behaviour changed on
  Android New Architecture.

**Why it applies.** ADR-0001 states that a window on an inactive Hyprland workspace receives no frame callbacks,
so a timer fallback is mandatory; [core#57592](https://github.com/react/react-native/issues/57592) is what the
absence of that fallback looks like from JS. And ordering: our `TimerManager` (#171) hands rAF callbacks to the
JS thread in some order — the spec says registration order, once per frame — and Reanimated's rAF-on-FrameClock
(#136) inherits whatever we do.

- **Epic:** #174 (M2), beside #59. Priority P1.
- **DEDUPE:** #59 owns "animation must not stop when the frame source stops" (the native driver); #171 owns a
  teardown crash; #136 owns worklets' rAF. JS-visible rAF order and idle liveness are unowned.
- **Proposed title:** `test(core): requestAnimationFrame is FIFO and never starves — callbacks run in registration order, once per frame, and keep running on the timer fallback while the surface is idle`
- **Acceptance:** unit (100 % gate) — 1,000 registrations dispatched in order, re-registration inside a callback
  lands in the *next* frame; e2e — the harness withholds `wl_surface.frame` for 2 s and a bundle's rAF counter
  keeps advancing at the fallback rate; TSan — the fallback timer racing a late frame callback.

---

## Group G — Accessibility (M5 groundwork, M2 assertion surface)

### G1. State and value changes emit events without a remount

- [core#35774](https://github.com/react/react-native/issues/35774) — open, +10, c15: `accessibilityValue` out of
  sync on iOS.
- [core#56296](https://github.com/react/react-native/issues/56296) — open, 2026-03: the `expanded: false` state is
  ignored by Fabric; [core#45300](https://github.com/react/react-native/issues/45300) — open, c4: state treated
  incorrectly on iOS; [core#45096](https://github.com/react/react-native/issues/45096) — closed, c10: TalkBack
  announces role and checked state in the wrong order.

**Why it applies.** #61 maps `accessibilityState`/`Value` into the AT-SPI state set on mount. The upstream bugs are
all about the *update*: a prop change must emit `object:state-changed:<state>` and
`object:property-change:accessible-value` on the bus, with the node's identity unchanged. Screen readers on Linux
(Orca) subscribe to those events; without them a toggle never announces.

- **Epic:** #177 (M5). Priority P2.
- **DEDUPE:** #61 item 2 covers the mapping; #92 covers announcements, focus and caret events, text scale and
  high contrast — not state-changed/property-change events. Partial.
- **Proposed title:** `test(a11y): accessibilityState and accessibilityValue updates emit AT-SPI state-changed and property-change events without a remount`
- **Acceptance:** unit (100 % gate) — the diff of two state sets to the event list; e2e — the harness's bus
  listener records exactly one `state-changed:checked` per toggle, and the accessible object path is stable.

### G2. Reading order is the visual order, per item

- [core#49462](https://github.com/react/react-native/issues/49462) — open, +2, c6: reading order is not meaningful
  and focus does not return after a Modal/dropdown closes; [core#49429](https://github.com/react/react-native/issues/49429)
  — closed, c3: same report.
- [core#48028](https://github.com/react/react-native/issues/48028) — closed, c6: VoiceOver reads a FlatList
  horizontally *across* items instead of per item.

**Why it applies.** The AT-SPI child order of every node is ours to emit, and "visual order" is a function of
layout (row-major, RTL-aware) that a list cell must present as one unit unless the cell's children are themselves
accessible. Upstream is adding an explicit ordering prop; when it lands in our pinned minor the same test extends
to it.

- **Epic:** #177 (M5). Priority P2.
- **DEDUPE:** #91 gates *keyboard* traversal order and focus return after an overlay; #61 item 5 says the exposed
  tree is the painted tree. Screen-reader child order per list item is not asserted.
- **Proposed title:** `test(a11y): the AT-SPI child order is the visual reading order — list cells read per item, RTL reverses rows, and an explicit ordering prop reorders children when upstream ships it`
- **Acceptance:** e2e — the #216 tree dump for a 3×3 grid and a FlatList lists children in row-major order and
  one node per cell; unit (100 % gate) — the ordering comparator over layout rectangles in LTR and RTL.

### G3. A nested `Text` link is its own focusable node

- [core#53243](https://github.com/react/react-native/issues/53243) — open, +1, c3: nested `<Text role="link">` not
  focusable on iOS.
- [core#35194](https://github.com/react/react-native/issues/35194) — closed, c8: nested text not focusable by
  hardware keyboard.
- [web#1266](https://github.com/necolas/react-native-web/issues/1266) — closed, +1, c7: `accessibilityRole="link"`
  not focusable; [web#2094](https://github.com/necolas/react-native-web/issues/2094) — closed: Enter on a focused
  `Text` does not invoke `onPress`.

**Why it applies.** A paragraph is one scene node but may contain several interactive fragments; each fragment
with `onPress` or `role="link"` needs a focus rectangle (its glyph-run bounds), a Tab stop, Enter activation, and
an AT-SPI `Hyperlink` on the parent's `Hypertext` interface. No widget toolkit gives that for free; SkParagraph
gives the run rectangles, which is all it takes.

- **Epic:** #177 (M5). Priority P2.
- **DEDUPE:** #43 owns nested `Text` painting and `onPress` hit-testing; #37 owns the focus model for nodes.
  Fragments as focus targets are unowned.
- **Proposed title:** `feat(a11y): a nested Text fragment with role=link is its own focusable node — Tab reaches it, Enter activates it, and AT-SPI exposes it as a Hyperlink`
- **Acceptance:** golden — the focus ring around a wrapped two-line link fragment; e2e — Tab order includes the
  fragment and Enter fires its `onPress`; unit (100 % gate) — fragment-to-rectangle mapping across a line break.

### G4. `accessibilityActions` over the AT-SPI Action interface

- [core#47268](https://github.com/react/react-native/issues/47268) — closed, c22: custom actions "not supported"
  on Android with the documented example; [core#48638](https://github.com/react/react-native/issues/48638) —
  closed, +3, c3: actions not triggering on a FlatList carousel; [core#53496](https://github.com/react/react-native/issues/53496)
  — closed, +2, c2: action labels not read by VoiceOver.
- [rnw#3474](https://github.com/microsoft/react-native-windows/issues/3474) — open, c1: typed action names
  (`invoke`, `toggle`, `expand`, `collapse`, …).

**Why it applies.** `accessibilityActions` + `onAccessibilityAction` is the only way a screen-reader user
increments a slider or dismisses a card without a pointer. AT-SPI's `Action` interface (`GetNActions`,
`GetName`, `DoAction`) is a direct mapping and is small.

- **Epic:** #177 (M5). Priority P2.
- **DEDUPE:** #27, #61, #92 and #216 do not mention actions.
- **Proposed title:** `feat(a11y): accessibilityActions and onAccessibilityAction over the AT-SPI Action interface — activate, increment, decrement and named custom actions`
- **Acceptance:** unit (100 % gate) — the standard-name table and custom-name pass-through; e2e — `DoAction(0)`
  from the harness's bus client fires `onAccessibilityAction` with the right name.

---

## Dropped: already covered, with the upstream numbers to paste into the owning issue

| Candidate | Upstream retrieved this pass | Owned by |
| --- | --- | --- |
| Drag/momentum brackets for wheel input | [web#2249](https://github.com/necolas/react-native-web/issues/2249) open +9 c7, [web#2247](https://github.com/necolas/react-native-web/issues/2247) | #45 gap 3 names `onScrollBeginDrag`/`EndDrag` per wheel flick and touchpad gesture explicitly |
| Nested `Pressable` hover state to parent | [web#1875](https://github.com/necolas/react-native-web/issues/1875) open +12, [web#1708](https://github.com/necolas/react-native-web/issues/1708) c38 | #36 — the enter/leave bubbling it needs; Pressability derives the state in unmodified JS |
| Horizontal ScrollView touchables under a sticky header | [core#51290](https://github.com/react/react-native/issues/51290) open +16 c20 | #98 |
| `onChangeText` twice / `onKeyPress` after `onChange` / non-ASCII key empty | [rnw#12780](https://github.com/microsoft/react-native-windows/issues/12780), [rnw#14959](https://github.com/microsoft/react-native-windows/issues/14959), [core#48636](https://github.com/react/react-native/issues/48636) c9, [core#37967](https://github.com/react/react-native/issues/37967) +3 c22, [core#45199](https://github.com/react/react-native/issues/45199) | #54 (ordering) and #65 (key identity) |
| Inline `View` inside `Text` clipping | [rnw#14443](https://github.com/microsoft/react-native-windows/issues/14443), [rnw#6315](https://github.com/microsoft/react-native-windows/issues/6315), [rnw#5689](https://github.com/microsoft/react-native-windows/issues/5689) | #43 and #111 item 6 |
| `ImageBackground` under padding / percentage height | [core#48803](https://github.com/react/react-native/issues/48803) +3 c12, [core#50539](https://github.com/react/react-native/issues/50539) c7, [core#33513](https://github.com/react/react-native/issues/33513) | #44 (resizeMode matrix) and #116 (padded parent) |
| Modal children painted at the origin for the first frames | [core#50442](https://github.com/react/react-native/issues/50442) closed +18 c27, [core#49717](https://github.com/react/react-native/issues/49717) open +5 | #62 plus the landed first-frame proofs (#222, #223) |
| Roles with no effect / ARIA Core-AAM mapping | [core#50123](https://github.com/react/react-native/issues/50123) open +29 c9, [core#43266](https://github.com/react/react-native/issues/43266) open +13 c20, [rnw#11432](https://github.com/microsoft/react-native-windows/issues/11432) | #61 — amend it to name the ARIA Core-AAM table as the mapping oracle and #50123's list as the negative fixture |
| Live regions | [core#34735](https://github.com/react/react-native/issues/34735) closed +1 c13 | #92 |
| Text a11y interfaces (`ITextProvider`, labelled-by via `nativeID`) | [rnw#14333](https://github.com/microsoft/react-native-windows/issues/14333), [rnw#12514](https://github.com/microsoft/react-native-windows/issues/12514) | #27, #92, and E1 above for the relation |
| Per-component functional and snapshot e2e suites | [rnw#12458](https://github.com/microsoft/react-native-windows/issues/12458)–[#12473](https://github.com/microsoft/react-native-windows/issues/12473), [rnw#11296](https://github.com/microsoft/react-native-windows/issues/11296)–[#11313](https://github.com/microsoft/react-native-windows/issues/11313), [rnw#13238](https://github.com/microsoft/react-native-windows/issues/13238), [rn-macos#1740](https://github.com/microsoft/react-native-macos/issues/1740) | #213, #231, #232, #234 |
| Text redraws and font-scale invalidation | [rnw#12024](https://github.com/microsoft/react-native-windows/issues/12024), [rnw#13864](https://github.com/microsoft/react-native-windows/issues/13864) | #42 and #113 |
| Blurry scale animations, hairline separators, occasional truncation, cursor API | [rnw#4117](https://github.com/microsoft/react-native-windows/issues/4117), [rnw#4047](https://github.com/microsoft/react-native-windows/issues/4047) c4, [rnw#9343](https://github.com/microsoft/react-native-windows/issues/9343) c14, [rnw#9454](https://github.com/microsoft/react-native-windows/issues/9454) c11 | #103, #100, #41/#111, #40 |
| `Dimensions` window vs screen under translucent chrome | [core#41918](https://github.com/react/react-native/issues/41918) open +5 c20, [core#49511](https://github.com/react/react-native/issues/49511) | #50 and #51 — amend #50 to state that `window` is the `xdg_toplevel` content box and `screen` the current `wl_output` |

Skipped without a table row because they are native-widget-only with no canvas analogue: Android ripple
([core#34553](https://github.com/react/react-native/issues/34553) +19), `statusBarTranslucent` Modal height,
dictation, `KeyboardAvoidingView` (no soft keyboard on a bare Wayland surface in M1), CocoaPods/Xcode build
failures, and react-native-web's `className`/DOM-loader issues.

---

## The ten highest-value candidates

1. **A2 `maintainVisibleContentPosition`** — unowned, open upstream on both mobile platforms and Windows, and it is
   the one-frame-jump class our first-frame proofs already know how to catch.
2. **A6 press cancelled by scroll** — a stuck pressed row in every list is the first thing a desktop user notices,
   and React Native has no responder hand-off for wheel input, so only the platform can write it.
3. **C1 colour emoji and fontconfig fallback** — the text choice ADR-0001 made is tested by emoji before anything
   else, and no issue owns it.
4. **C8 multiline `TextInput` inner scrolling** — every platform including macOS Fabric has it broken today, and
   `TextInput` is in the M1 definition of "working".
5. **A1 snapping and paging** — three open react-native-windows issues for what is, on a canvas, one function in
   the physics we already own.
6. **F1 rAF FIFO and idle liveness** — the timer fallback ADR-0001 calls mandatory has no JS-visible test, and
   Reanimated's rAF will inherit our ordering.
7. **A3 `contentInset`/`contentOffset`** — a +14 upstream regression that is exactly "the hit rectangle is the
   content box", which is the naive canvas implementation.
8. **D1 animated GIF and WebP** — the 120 Hz-first platform is the one most likely to write [core#33039](https://github.com/react/react-native/issues/33039).
9. **B3 focus-visible and scroll-into-view** — web's five-year accumulation of focus edge cases, takeable as one
   contract on top of #37.
10. **C7 `TextInput` placeholder** — nine upstream issues for two invariants (paint with the input's style; caret at
    the first-glyph position) that are a single golden on a canvas.
