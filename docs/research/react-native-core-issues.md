# What `facebook/react-native` issues teach `react-native-linux`

- Source: `react/react-native` (the repository `facebook/react-native` now redirects to; the old owner name is
  rejected by the search API, which is itself the first finding — every hard-coded `facebook/react-native` URL in
  our docs still resolves through the redirect, but search qualifiers do not). Read-only via the GitHub search
  API on 2026-09-02.
- Corpus: **27,818 issues** total (`is:issue`, excludes PRs), **702 open**. Restricted to the Fabric era by
  `created:>=2022-01-01` — React Native 0.68 (2022-03) was the first release with a New Architecture opt-in — the
  corpus is **5,690 issues, 605 open**. Inside it: **704** carry `Type: New Architecture` (77 open, **78**
  `Resolution: Fixed`), 995 `Platform: iOS`, 1,197 `Platform: Android`, 1,445 `Needs: Repro`, 482
  `Issue: Author Provided Repro`, 51 `Impact: Regression`.
- Sampled with **121 themed queries** in two batteries (title-scoped, full-text, and label-scoped), each sorted by
  reactions or by comments, top 8–10 per query: **341 distinct issues read**. The query list is in
  *Corpus and sampling*, below; the scripts that produced it are reproducible from that list alone.
- The machine-readable plan this document explains is `scripts/issue-plans/core.json`: **30 proposed issues**,
  all of them "check what we already built, then add the test that would have caught this". Every number in every
  table below is cited in at least one of those 30 bodies; none was invented for this write-up.
- `core.json` uses the same label scheme as `macos.json` and `windows.json` with exactly one addition:
  **`origin:rn-core`**. Provenance stays auditable — every issue this batch produces is findable by the tracker
  that produced its lesson.

## Why upstream's own tracker is the third and sharpest ancestor

`react-native-macos` taught us what breaks when React Native meets a desktop widget toolkit.
`react-native-windows` taught us what breaks when an out-of-tree fork survives ten years. Upstream teaches
something neither could: **what breaks in the parts we do not get to reimplement.**

We vendor `ReactCommon` — `ShadowTree`, `MountingCoordinator`, Yoga, `BaseViewProps::resolveBorderMetrics`,
`resolveTransform`, the animated node graph. When upstream reports "assertion failed in `ShadowTree.cpp` commit"
(#51870) or "`YGNodeGetOwner(childYogaNode) == &yogaNode_`" (#52349), that is not a lesson about a foreign
platform's bug — it is a report about **code that is already linked into `rnl_window`**, filed by people running
it at a scale we will not reach for years. Where the macOS and Windows studies produced *parity* work, this one
produces *regression detection* for code we inherited and did not write.

Three caveats, stated so the mapping stays honest:

1. **iOS and Android are widget platforms; we are a canvas.** A large share of the tracker is
   `UITextView`/`android.widget.TextView` integration. Where the bug is an integration seam, it usually converts
   into "unwritten code" for us rather than "a bug we will also have" — but the *symptom* is still the right
   acceptance test, because it is what a user notices. Each group states which way it converts.
2. **View recycling is theirs, not (yet) ours.** iOS Fabric recycles `RCTViewComponentView` instances via
   `prepareForRecycle`; a whole family of upstream bugs (#55090, #53050, #55768) is state surviving that reuse.
   Our `RetainedScene` reuses *nodes* across mounting transactions rather than *views*, which is a different
   mechanism with the same failure mode. Group 14 converts it rather than importing it.
3. **Closed does not mean fixed.** Upstream's stale bot closes aggressively — #34722 ("when one border of a View
   is transparent, all borders vanish") is `Stale`-closed and still describes the exact deviation
   `docs/cpp-toolchain.md` records as ours. Where a fix actually landed the tables say `Resolution: Fixed`, and
   those issues are weighted highest, because a landed fix is a specification with a known-good answer.

---

## Group 1 — Hit-testing disagrees with animated, scrolled and out-of-flow geometry

| Issue | State | Signal | Symptom | Platform |
| --- | --- | --- | --- | --- |
| [#51621](https://github.com/facebook/react-native/issues/51621) | open, `Resolution: PR Submitted` | 28r / 5c | `onPress` does not fire **while** a view is animating on the new architecture; works on the old one | iOS + Android |
| [#36710](https://github.com/facebook/react-native/issues/36710) | closed | 9r / 183c | "Touchable components stop responding randomly" — the longest comment thread in the era | Android |
| [#36504](https://github.com/facebook/react-native/issues/36504) | closed, `Resolution: Fixed` | 3r / 26c | `onPress` never called after a `useNativeDriver` transform animation ran | both |
| [#44643](https://github.com/facebook/react-native/issues/44643) | closed | 1r / 51c | "Some touchable components are not working" | Android |
| [#44768](https://github.com/facebook/react-native/issues/44768) | closed | — / 5c | Animated children with `translate` inside a horizontal `Animated.ScrollView` are not pressable at the painted position | Android |
| [#35612](https://github.com/facebook/react-native/issues/35612) | closed | 2r / 5c | `Animated.View` passes touches to a **sibling** when its child is absolutely positioned | both |
| [#50589](https://github.com/facebook/react-native/issues/50589) | open | — / 5c | Touch events cause `zIndex` to become invalid | iOS + Android |
| [#34542](https://github.com/facebook/react-native/issues/34542) | open | 3r / 5c | Absolutely positioned element outside its parent's bounds receives no gestures | Android |
| [#37181](https://github.com/facebook/react-native/issues/37181) | closed | 3r / 6c | Long-press/caret placement dead on a `position:absolute` `TextInput` outside parent bounds | Android |
| [#50465](https://github.com/facebook/react-native/issues/50465) | closed | 1r / 9c | `TextInput`/`Pressable` with `opacity: 0` receive no touches — opacity leaked into the hit path | iOS |
| [#54271](https://github.com/facebook/react-native/issues/54271) | closed | — / 3c | New arch: a child touchable inside `pointerEvents="box-only"` no longer calls the parent's `onPress` | both |
| [#33022](https://github.com/facebook/react-native/issues/33022) | closed | 2r / 6c | `pointerEvents` ignored on scroll views | iOS |
| [#54988](https://github.com/facebook/react-native/issues/54988) | open | — / 1c | `measure()` returns **doubled** values when a parent is `scale`d — transform leaked into layout geometry | iOS + Android + web |

**Cause pattern.** Upstream has the same two-pipeline shape the macOS study named, plus a third participant that
macOS never had: an **animation driver that writes geometry outside the commit**. #51621 is the specimen. The
shadow tree holds the committed transform; the native driver holds the animated one; hit-testing reads the shadow
tree. Upstream's partial fix (PR 43374) syncs the animated value back into the shadow tree **on animation end**,
which is why the bug survives *during* the animation and closes as soon as it stops. #54988 is the same disease
inverted: `transform` is documented as not affecting layout, and yet it reaches `measure()`.

**Why this reaches Linux.** Directly, and worse. `docs/cpp-toolchain.md` records that hit testing is upstream's
(`PointerEventsProcessor`, reading `LayoutMetrics`) while transform composition is ours in `RetainedScene`, which
reduces a 4×4 to a 2D affine before painting — a reduction the hit-test does not perform. #19 will add a
platform-owned frame thread writing transform/opacity per frame. The moment it does, we reproduce #51621's exact
configuration: painted geometry advanced by a thread that the hit path never consults. There is no iOS/Android
"widget tree" excuse available to us — both pipelines are ours to keep in agreement.

**Our coverage.** [#35](https://github.com/react-native-linux/react-native-linux/issues/35) (open, P0) asserts
hit/paint agreement for `transform`, `zIndex` and `overflow` on a **static** tree.
[#64](https://github.com/react-native-linux/react-native-linux/issues/64) owns `pointerEvents` semantics.
**Gap:** the animated case, the scrolled/sticky case, and `measure()` returning layout rather than paint. Two new
issues; #35 gets an amendment note (see *Amendments*).

---

## Group 2 — The border box is one clip, and every other feature has to respect it

| Issue | State | Signal | Symptom | Platform |
| --- | --- | --- | --- | --- |
| [#34722](https://github.com/facebook/react-native/issues/34722) | closed (`Stale`) | 12r / 17c | One transparent border side makes **all four** sides disappear | Android |
| [#33862](https://github.com/facebook/react-native/issues/33862) | closed | 11r / 14c | `resizeMode:'contain'` + `borderRadius`: the edge pixel is smeared to fill the box | both |
| [#32918](https://github.com/facebook/react-native/issues/32918) | **open** | 10r / 27c | `borderStyle:'dashed'` is drawn wrong | both |
| [#34553](https://github.com/facebook/react-native/issues/34553) | **open** | 19r / 14c | Pressable ripple paints **outside** the rounded border | Android |
| [#34073](https://github.com/facebook/react-native/issues/34073) | closed | 6r / 12c | Image stretches as soon as `borderRadius` appears | both |
| [#49606](https://github.com/facebook/react-native/issues/49606) | **open** | — / 8c | Borders with opacity **and** radius do not draw | Android |
| [#39286](https://github.com/facebook/react-native/issues/39286) | closed | 2r / 7c | Border-colour alpha invisible when the background under it is opaque — border composited against the wrong layer | iOS |
| [#57780](https://github.com/facebook/react-native/issues/57780) | closed, completed | — / 2c | Removing (not zeroing) `borderWidth` on a rounded `overflow:hidden` View stops **all children** drawing — NaN reaches the padding-box clip | Android |
| [#48078](https://github.com/facebook/react-native/issues/48078) | **open** | — / 5c | `dashed`/`dotted` stopped working in combination with `overflow:hidden` in 0.76 | iOS |
| [#42289](https://github.com/facebook/react-native/issues/42289) | closed | — / 7c | `View`/`Pressable` lose their background when `borderStyle` is dashed and `borderWidth` is 0 | iOS |
| [#33950](https://github.com/facebook/react-native/issues/33950) | closed | — / 5c | Vertical borders leave **gaps** on rounded Views — the corner-seam artifact | Android |
| [#58054](https://github.com/facebook/react-native/issues/58054) | **open** | 1r / 0c | `StyleSheet.hairlineWidth` borders not always rendered | both |
| [#51489](https://github.com/facebook/react-native/issues/51489) | closed | — / 9c | Artifacts on a two-corner-rounded View when the app is inactive | iOS |
| [#50029](https://github.com/facebook/react-native/issues/50029) / [#41226](https://github.com/facebook/react-native/issues/41226) | closed | — / 9c, 3r | Jagged, aliased rounded corners | Android |
| [#53977](https://github.com/facebook/react-native/issues/53977) | closed | — / 3c | Percentage `borderRadius` on an `<Image>` **crashes** on Android API 35 | Android |
| [#51193](https://github.com/facebook/react-native/issues/51193) | closed | 7r / 3c | `borderTopEndRadius`/`borderBottomEndRadius` resolve inconsistently under RTL | Android |
| [#38124](https://github.com/facebook/react-native/issues/38124) | closed | — / 6c | Child with `borderRadius` draws wrong when the **parent** has `transform: rotate` | Android |
| [#49442](https://github.com/facebook/react-native/issues/49442) | **open** | — / 4c | A gradient artifact appears at `borderTopLeftRadius` inside a scaled View | iOS |

**Cause pattern.** `borderRadius` is not a decoration, it is the shape of the node's **clip**, its **background
fill**, its **border ring**, its **content box for images and gradients**, and — per Group 1 — its **hit region**.
Every platform implements those five independently and then discovers they disagree. Two subspecies recur: an
*unset vs. zero* confusion (#34722, #42289, #57780 — the absent value is not the neutral value), and a
*composition* failure where the corner survives one feature and not its combination with another (#48078, #38124,
#49442, #33862).

**Why this reaches Linux.** This is the group we are **most exposed to and best positioned on**. Most exposed:
`docs/cpp-toolchain.md` already documents, as deliberate deviations, three of the exact bugs in this table —
"an unset `borderColor` draws nothing" (#34722's shape), "anti-aliased wedge clips can leave a hairline seam"
(#33950's shape), and "`borderStyle` is ignored" (#32918/#48078/#42289's whole subtree). Best positioned:
`resolveBorderMetrics` is upstream's own C++, so the *arithmetic* is shared with iOS and Android and its
percentage/clamp behaviour is testable against their answers, and the resulting `SkRRect` is one object that the
fill, the ring, the clip stack and the hit region can all be required to derive from.

**Our coverage.** [#13](https://github.com/react-native-linux/react-native-linux/issues/13) (closed) implemented
the props and recorded the deviations. [#69](https://github.com/react-native-linux/react-native-linux/issues/69)
(open, P0) owns prop-coverage conformance as a matrix. **Gap:** no test forces the five consumers of the rounded
box to agree, no golden covers the unset-vs-zero cases, and `borderStyle` has a documented deviation with no
issue. Three new issues.

---

## Group 3 — Shadows, effects, and composition order with transforms and clips

| Issue | State | Signal | Symptom | Platform |
| --- | --- | --- | --- | --- |
| [#48874](https://github.com/facebook/react-native/issues/48874) | closed | 7r / 20c | `elevation` combined with `borderRadius` "looks weird" in 0.77 | Android |
| [#34320](https://github.com/facebook/react-native/issues/34320) | closed | — / 4c | Shadows draw incorrectly once the view has a transform | Android |
| [#50775](https://github.com/facebook/react-native/issues/50775) | closed | — / 3c | Initial `transform: scale != 1` makes `boxShadow` not match the view's layout | both |
| [#54612](https://github.com/facebook/react-native/issues/54612) / [#54556](https://github.com/facebook/react-native/issues/54556) | open / closed | — / 1c, 3c | Children **inherit** the parent's properties when the parent has `boxShadow` + `scale` + `overflow:hidden` | both |
| [#47920](https://github.com/facebook/react-native/issues/47920) | closed | — / 12c | `boxShadow` on a `TextInput` alternates visible/hidden on every keystroke | Android |
| [#52703](https://github.com/facebook/react-native/issues/52703) | closed | — / 8c | `boxShadow` does nothing with `renderToHardwareTextureAndroid` — a layer promotion dropped the effect | Android |
| [#51436](https://github.com/facebook/react-native/issues/51436) | closed | 1r / 3c | `boxShadow` values accepted in 0.78 rejected in 0.79 — parser drift | both |
| [#37078](https://github.com/facebook/react-native/issues/37078) | closed | 5r / 5c | A rotated view is clipped by its background image | iOS |
| [#34425](https://github.com/facebook/react-native/issues/34425) | closed | **98r** / 62c | ☂️ "Web Styles (Part 1)" — the CSS-convergence umbrella: `boxShadow`, `filter`, `mixBlendMode`, `outline` | — |
| [#34424](https://github.com/facebook/react-native/issues/34424) | closed | 40r / 26c | ☂️ "Web Props (Part 1)" — the sibling prop-surface umbrella | — |

**Cause pattern.** A shadow is the only View feature that paints **outside** the node's own box, so it is the one
feature whose interaction with every clip and every transform must be decided explicitly: is the shadow drawn in
the node's local space and then transformed (so it scales), or drawn in the parent's space at the transformed
outline (so it does not)? Upstream answers differently per platform and per code path, which is exactly #50775 and
#34320. #54612 is the most diagnostic entry: `boxShadow` + `scale` + `overflow:hidden` makes children *inherit
properties*, i.e. the effect forced a layer promotion and the layer's paint leaked down the tree.

**Why this reaches Linux.** Fully, and it is unbuilt: `docs/cpp-toolchain.md` lists `shadowColor`/elevation,
`boxShadow`, `filter`, `mixBlendMode`, `isolation` and `outline*` as "not implemented at all". On Skia every one
of these is a `saveLayer` decision, and `saveLayer` is precisely where composition order stops being implicit. The
98-reaction umbrella #34425 is also the strongest external evidence for our ADR-0001 canvas argument: React
Native's style surface is converging on CSS faster than any widget-backed platform can follow, and a canvas is the
only implementation where that convergence is cheap.

**Our coverage.** [#67](https://github.com/react-native-linux/react-native-linux/issues/67) (`boxShadow`, P1) and
[#68](https://github.com/react-native-linux/react-native-linux/issues/68) (`filter`, `mixBlendMode`, `isolation`,
`outline`, P2), both from the Windows batch. **Gap:** neither states the composition order against transforms and
clips, which is the part upstream actually gets wrong. One new issue; #67 gets an amendment note.

---

## Group 4 — Raster scale and rasterization cost

| Issue | State | Signal | Symptom | Platform |
| --- | --- | --- | --- | --- |
| [#48673](https://github.com/facebook/react-native/issues/48673) | **open** | 4r / 7c | Inverse nested scale (parent ×100, child ×0.01) renders the child **pixelated and blurred** | iOS |
| [#56980](https://github.com/facebook/react-native/issues/56980) | **open** | — / 4c | Fabric's first mounting transaction calls `RCTGetBorderImage` thousands of times: ~18,900 raster regions, 6.9 GB, Hermes OOM at launch | iOS |
| [#51869](https://github.com/facebook/react-native/issues/51869) | closed, `Resolution: Fixed` | **44r** / 27c | Mounting is ~1 ms **per view** on the new architecture | Android |
| [#47490](https://github.com/facebook/react-native/issues/47490) | closed | **60r** / 134c | "Performance issues with Bridgeless" — the most-reacted new-architecture issue in the era | both |
| [#36296](https://github.com/facebook/react-native/issues/36296) | closed | 46r / 171c | Very long re-rendering times in 0.71.3 | both |
| [#57198](https://github.com/facebook/react-native/issues/57198) | **open** | 11r / 0c | Memory not reclaimed after unmounting a screen with many components (Fabric, 0.86) | both |
| [#49442](https://github.com/facebook/react-native/issues/49442) | **open** | — / 4c | Gradient artifact at a rounded corner inside a scaled View — resampling at the wrong scale | iOS |
| [#51333](https://github.com/facebook/react-native/issues/51333) | closed | 1r / 2c | A high-resolution image fails to render on Android and leaks memory on iOS | both |

**Cause pattern.** Two distinct costs, both invisible until they are fatal. **Resolution**: a subtree is
rasterized at one scale and then transformed by another, so the pixels are wrong at the scale they are finally
shown at (#48673, #49442). **Allocation**: a decoration is re-rasterized per node per commit instead of being
cached by its parameters, and a large first mount multiplies it (#56980, #51869). Note that #56980 presents as a
*JavaScript engine OOM* — the report blames Hermes; the cause is border rasterization on the main thread.

**Why this reaches Linux.** This is the group where being a canvas converts a widget-platform bug into a design
obligation. Skia will happily rasterize a `saveLayer` at the ambient device scale and let a subsequent matrix
magnify it, which is #48673 exactly. `wp_fractional_scale_v1` gives us a second, runtime-variable scale factor
that iOS/Android do not have to re-derive mid-session. And `docs/cpp-toolchain.md` records that "every paint
rebuilds the paragraph and every snapshot copies the attributed string" — an allocation profile in the same family
as #56980, currently with no probe to see it.

**Our coverage.** [#12](https://github.com/react-native-linux/react-native-linux/issues/12) (damage tracking,
closed), [#20](https://github.com/react-native-linux/react-native-linux/issues/20) (pacing/Tracy, open),
[#51](https://github.com/react-native-linux/react-native-linux/issues/51) (fractional-scale change, open),
[#76](https://github.com/react-native-linux/react-native-linux/issues/76) (teardown leak gate, open). **Gap:** no
raster-scale correctness test, no allocation ceiling on a mounting transaction, no per-view mount cost number.
Three new issues.

---

## Group 5 — Text vertical metrics: `lineHeight`, leading, and clipped glyphs

| Issue | State | Signal | Symptom | Platform |
| --- | --- | --- | --- | --- |
| [#53286](https://github.com/facebook/react-native/issues/53286) | closed | **48r** / 62c | Text rendering cut off on Android 15 & 16, both architectures | Android |
| [#56402](https://github.com/facebook/react-native/issues/56402) | **open** | 31r / 3c | Same, refiled 2026-04 | Android |
| [#53450](https://github.com/facebook/react-native/issues/53450) | closed | 14r / 43c | "Text not fully rendered on iOS" | iOS |
| [#49886](https://github.com/facebook/react-native/issues/49886) | closed, `Type: New Architecture` | 7r / 15c | Descenders clipped after 0.76→0.78 when `lineHeight == fontSize` | Android |
| [#39145](https://github.com/facebook/react-native/issues/39145) | **open** | 13r / 22c | `lineHeight` not distributed evenly in a `TextInput` — half-leading applied on one side | both |
| [#46436](https://github.com/facebook/react-native/issues/46436) | **open** | 10r / 5c | Extra line wrap on some devices for some `lineHeight` + `letterSpacing` combinations | Android |
| [#48921](https://github.com/facebook/react-native/issues/48921) | **open** | 5r / 25c | `selectable` breaks truncation **and** `lineHeight` | Android |
| [#53307](https://github.com/facebook/react-native/issues/53307) | closed | 3r / 8c | Incorrect `lineHeight` on Android 15 | Android |
| [#37764](https://github.com/facebook/react-native/issues/37764) | **open** | 7r / 6c | Text does not show all its content on some devices | Android |
| [#33704](https://github.com/facebook/react-native/issues/33704) | closed | — / 3c | Tibetan text clipped on multiline with a given line height | Android |
| [#35951](https://github.com/facebook/react-native/issues/35951) | closed | 6r / 6c | Wrong `TextInput` height when `lineHeight` is set | Android |
| [#53092](https://github.com/facebook/react-native/issues/53092) | closed | 4r / 16c | Nested-`Text` alignment broken on new-arch iOS when `lineHeight` is provided | iOS |

**Cause pattern.** `lineHeight` forces a choice with no neutral answer: when the requested line box is smaller
than the font's natural ascent+descent, something must be clipped, scaled, or allowed to overflow — and the
decision must be identical in the code that *measures* the paragraph and the code that *paints* it. Every entry
above is one platform changing that decision (often because the OS text stack changed under it: Android 15, iOS
26) and the other half of the pipeline not following. The tallest signal in the entire Fabric-era text corpus is
this one bug, filed three times (#53286, #56402, #53450) with 93 reactions between them.

**Why this reaches Linux.** Fully. We own both halves: `layoutParagraph` answers Yoga and the painter draws from
the same inputs, and `docs/cpp-toolchain.md` states our `lineHeight` implementation converts points into
SkParagraph's multiple-of-font-size `height` **with half leading** — i.e. we have already made exactly the choice
whose asymmetry #39145 reports. Our text is drawn to a GPU glyph atlas with no OS text view to clip against, so
"clipped descender" becomes "wrong line box height", which is silent unless a golden looks at it.

**Our coverage.** [#41](https://github.com/react-native-linux/react-native-linux/issues/41) (open, P0) requires
the measured paragraph to equal the painted paragraph — the right container, but it says nothing about vertical
metrics or about `lineHeight < fontSize`. [#14](https://github.com/react-native-linux/react-native-linux/issues/14)
closed with the fidelity list. **Gap:** a vertical-metrics conformance matrix with goldens. One new issue, plus an
amendment to #41.

---

## Group 6 — Text box sizing: measurement disagreeing with the shaped paragraph

| Issue | State | Signal | Symptom | Platform |
| --- | --- | --- | --- | --- |
| [#54571](https://github.com/facebook/react-native/issues/54571) | **open** | 10r / 4c | Multi-line `Text` can no longer shrink its width to fit its content | both |
| [#54182](https://github.com/facebook/react-native/issues/54182) | closed | 5r / 4c | Last line of a paragraph missing inside a `ScrollView` | iOS |
| [#55468](https://github.com/facebook/react-native/issues/55468) | **open** | — / 5c | An **empty** `<Text>` has non-zero width | iOS |
| [#52941](https://github.com/facebook/react-native/issues/52941) | closed | — / 5c | `alignItems:'center'` behaves differently on multiline `Text` in 0.79 | both |
| [#41476](https://github.com/facebook/react-native/issues/41476) | closed | 4r / 8c | `TextView` does not work with dynamic heights under Fabric | Android |
| [#54552](https://github.com/facebook/react-native/issues/54552) | **open** | 2r / 7c | `onTextLayout` reports multiple lines for text that renders on one | both |
| [#37902](https://github.com/facebook/react-native/issues/37902) | **open** | 6r / 11c | `onTextLayout` never fires | both |
| [#35276](https://github.com/facebook/react-native/issues/35276) | **open** | 1r / 3c | Baseline alignment ignores `numberOfLines` | both |
| [#39722](https://github.com/facebook/react-native/issues/39722) | closed | — / 9c | `numberOfLines={1}` + `alignSelf:'flex-start'` breaks the text mid-word | Android |
| [#37926](https://github.com/facebook/react-native/issues/37926) | **open** | — / 7c | The ellipsis carries the background colour of the text it replaced | iOS |
| [#48727](https://github.com/facebook/react-native/issues/48727) | **open** | — / 6c | An `<Image>` nested in `<Text>` with `lineHeight` overflows its container | both |
| [#52642](https://github.com/facebook/react-native/issues/52642) | **open** | 17r / 13c | Incorrect font scaling with `adjustsFontSizeToFit` | iOS |

**Cause pattern.** The Yoga measure callback and the paint pass construct the paragraph **twice**, from inputs
that are almost but not exactly the same — a different available width, a different max-lines, a stale cache, a
different fallback font. Where the two disagree the box is the wrong size and the glyphs are cropped or the
container is padded by nothing. `onTextLayout` (#54552, #37902) is the same disagreement made observable: it is
the only API that reports what the *painted* paragraph did, and it is wrong or absent.

**Why this reaches Linux.** Fully, and the shape is pre-diagnosed in our own docs: a measure cache sits in front
of the measure half only, `measureLines` and `onTextLayout` are absent (so we cannot even report the painted
result yet), inline attachments are sized from the attachment's own measured frame (#48727's exact mechanism), and
`adjustsFontSizeToFit`/`textAlignVertical` are ignored. The empty-`<Text>` case (#55468) is a one-line unit test
that we have no reason to be passing today.

**Our coverage.** #41 (open, P0), #14 (closed).
[#46](https://github.com/react-native-linux/react-native-linux/issues/46) (ScrollView content size on the first
frame) covers #54182's containing shape. **Gap:** box-sizing conformance and `onTextLayout` as the observability
hook. One new issue.

---

## Group 7 — Nested `<Text>`: inheritance, fragments, and bidi segmentation

| Issue | State | Signal | Symptom | Platform |
| --- | --- | --- | --- | --- |
| [#53343](https://github.com/facebook/react-native/issues/53343) | **open** | 8r / 23c | `<Text>` does not respect `color="transparent"` in 0.81 | Android |
| [#45925](https://github.com/facebook/react-native/issues/45925) | **open** | 2r / 14c | A border cannot be applied to a nested `<Text>` | both |
| [#54434](https://github.com/facebook/react-native/issues/54434) | closed | 2r / 11c | Arabic **ligatures break** when text is split across nested `<Text>` elements | Android |
| [#33418](https://github.com/facebook/react-native/issues/33418) | closed | 1r / 7c | `fontSize` on nested `<Text>` not always applied | Android |
| [#33431](https://github.com/facebook/react-native/issues/33431) | closed | 1r / 3c | `fontWeight`/`fontStyle` inheritance breaks on nested `<Text>` | Android |
| [#53092](https://github.com/facebook/react-native/issues/53092) | closed | 4r / 16c | Nested-text alignment broken on new-arch iOS with `lineHeight` | iOS |
| [#45938](https://github.com/facebook/react-native/issues/45938) | **open** | — / 11c | `<Text>` shows no content when both `borderWidth` and `backgroundColor` are set | Android |
| [#54217](https://github.com/facebook/react-native/issues/54217) | **open** | 3r / 5c | A child `<Text>` disappears after the **parent** `<View>`'s style changes | Android |
| [#37130](https://github.com/facebook/react-native/issues/37130) | closed | — / 5c | Text in a `FlatList` is not selectable | Android |
| [#50010](https://github.com/facebook/react-native/issues/50010) | closed | 1r / 4c | `selectable` does nothing on `<Text>` from 0.77 | Android |

**Cause pattern.** A `<Text>` tree is flattened into one attributed string, and everything that is *per-fragment*
(colour, background, border, weight, selectability) has to survive that flattening and then be projected back onto
glyph ranges. #54434 is the sharpest: splitting an Arabic string at a fragment boundary breaks shaping, because
shaping was performed per fragment instead of per paragraph — a **correctness** bug that only a script with
contextual forms reveals, and one no Latin-only golden would ever catch.

**Why this reaches Linux.** Fully. SkParagraph gives us per-fragment `TextStyle` (we already set `color`,
`backgroundColor`, weight, decoration per fragment) and shapes the whole paragraph, so #54434's cause is
structurally avoided — *provided* our attributed-string construction never splits a run at a fragment boundary,
which nothing currently asserts. Fragment backgrounds are implemented; fragment borders are not modelled at all.

**Our coverage.** [#43](https://github.com/react-native-linux/react-native-linux/issues/43) (open, P2) owns nested
`<Text>`, fragment `backgroundColor` extent and `selectable`. **Gap:** inheritance conformance and the bidi/ligature
segmentation guarantee. One new issue, scoped to tests, referencing #43.

---

## Group 8 — `TextInput`: content size, auto-grow, and recycled editors

| Issue | State | Signal | Symptom | Platform |
| --- | --- | --- | --- | --- |
| [#35590](https://github.com/facebook/react-native/issues/35590) | closed | **102r** / 107c | `TextInput` hangs with a third-party keyboard's grammar integration | Android |
| [#35936](https://github.com/facebook/react-native/issues/35936) | closed | 84r / 99c | ANRs using multiline `TextInput` | Android |
| [#39411](https://github.com/facebook/react-native/issues/39411) | closed | 48r / 93c | Keyboard flickering on `TextInput` with `secureTextEntry` | iOS |
| [#54570](https://github.com/facebook/react-native/issues/54570) | **open** | 23r / 9c | Uncontrolled multiline `TextInput` does not auto-grow | both |
| [#33165](https://github.com/facebook/react-native/issues/33165) | **open** | 22r / 38c | Multiline is jumpy and flaky when pressing Enter | both |
| [#53050](https://github.com/facebook/react-native/issues/53050) | **open**, `Type: New Architecture` | 17r / 19c | A **recycled** `TextInput` after autofill becomes non-editable and stops updating | iOS |
| [#52854](https://github.com/facebook/react-native/issues/52854) | **open** | 15r / 28c | `onContentSizeChange` never fires | iOS |
| [#46813](https://github.com/facebook/react-native/issues/46813) | **open** | 8r / 5c | Content jumps in a multiline input because the layout update lands **after** the content update | Android |
| [#47266](https://github.com/facebook/react-native/issues/47266) | **open** | 6r / 3c | `value` cannot be changed via `setNativeProps` after user input | iOS |
| [#46207](https://github.com/facebook/react-native/issues/46207) | **open** | 5r / 6c | Multiline + `lineHeight` height wrong when controlled | both |
| [#54304](https://github.com/facebook/react-native/issues/54304) | **open** | 3r / 7c | Multiline with a fixed height positions text inconsistently on first render | both |
| [#47359](https://github.com/facebook/react-native/issues/47359) | closed | 2r / 4c | Programmatic focus in `useEffect` fails **silently** in bridgeless mode | iOS |
| [#33649](https://github.com/facebook/react-native/issues/33649) | closed | 14r / 15c | `editable={false}` emits no `onPressIn`/`onPressOut` | both |
| [#51469](https://github.com/facebook/react-native/issues/51469) | **open** | — / 4c | Caret cannot be moved in an absolutely positioned `TextInput` whose parent has `zIndex` | Android |

**Cause pattern.** The macOS study called `TextInput` a state machine duplicated between a native editor and
React's controlled `value`. Upstream's Fabric-era corpus adds a *third* participant: **the content size**, which
is computed by the editor, fed back into layout, and read by the app — and whose update is ordered relative to the
text update by luck (#46813, #52854, #54570). Plus recycling (#53050): the editor object outlives the React node.

**Why this reaches Linux.** Fully and harder, per the macOS study — we have no editor to inherit. But note which
half converts: the ANR/keyboard-integration bugs (#35590, #35936, #39411) are Android IME plumbing and become
`zwp_text_input_v3` correctness for us, already owned by #26 and #17. The **content-size feedback loop** is not
inherited from anywhere: `<TextInput>`'s shadow node is ours, so the order of "text changed → re-measure →
commit → caret geometry" is a contract we write.

**Our coverage.** [#17](https://github.com/react-native-linux/react-native-linux/issues/17) (closed, the editor),
[#53](https://github.com/react-native-linux/react-native-linux/issues/53) (open, P0 parity matrix),
[#54](https://github.com/react-native-linux/react-native-linux/issues/54) (open, key/focus contract),
[#26](https://github.com/react-native-linux/react-native-linux/issues/26) (closed, IME). **Gap:** content size,
auto-grow and the update ordering. One new issue.

---

## Group 9 — `Image`: resizeMode arithmetic, decode memory, lifecycle

| Issue | State | Signal | Symptom | Platform |
| --- | --- | --- | --- | --- |
| [#33862](https://github.com/facebook/react-native/issues/33862) | closed | 11r / 14c | `contain` + `borderRadius` repeats the edge pixel across the container | both |
| [#42234](https://github.com/facebook/react-native/issues/42234) | **open** | 12r / 14c | An image with no file extension does not render | iOS |
| [#46095](https://github.com/facebook/react-native/issues/46095) | closed | 7r / 8c | GIFs stopped working after 0.74→0.75 | both |
| [#33131](https://github.com/facebook/react-native/issues/33131) / [#45404](https://github.com/facebook/react-native/issues/45404) | closed | 7r / 28c | `source.headers` are not sent with the request | both |
| [#48790](https://github.com/facebook/react-native/issues/48790) | closed | 4r / 16c | Images cut off / not rendered correctly on the new architecture | both |
| [#51198](https://github.com/facebook/react-native/issues/51198) | closed | 2r / 2c | A remote image's memory is not released when the `<Image>` unmounts | both |
| [#51333](https://github.com/facebook/react-native/issues/51333) | closed | 1r / 2c | High-resolution image fails to render (Android) and leaks (iOS) | both |
| [#35706](https://github.com/facebook/react-native/issues/35706) | closed | 5r / 25c | Crash in blur-radius rendering | iOS |
| [#43874](https://github.com/facebook/react-native/issues/43874) | closed | — / 2c | `<Image>` load events arrive with **no payload** | iOS |
| [#42132](https://github.com/facebook/react-native/issues/42132) | **open** | 2r / 6c | A GIF renders incorrectly with `resizeMode="repeat"` | both |
| [#40711](https://github.com/facebook/react-native/issues/40711) | closed | — / 9c | A resized image contains padding | both |
| [#34073](https://github.com/facebook/react-native/issues/34073) | closed | 6r / 12c | Image stretches when `borderRadius` is present | both |

**Cause pattern.** Three separable failures wearing one label. The **arithmetic** (`contain`/`cover`/`repeat`
against a rounded content box) is re-derived per platform and gets the degenerate cases wrong (#33862, #40711,
#42132). The **lifecycle** is under-specified: events fire without payloads (#43874), or a decoded bitmap outlives
its node (#51198, #51333). The **source resolution** rules are conventions nobody wrote down (#42234, #33131).

**Why this reaches Linux.** Fully. `docs/cpp-toolchain.md` records that we have no `srcSet`/scale selection, no
`onLoad*`/`onError` (though the `ImageResponseObserverCoordinator` upstream builds is already completed by our
decoder — the events are a matter of *emitting* them), no animated images, no `blurRadius`/`capInsets`, and that
the image fills the border box rather than the padding box. The cache has no probe and a failed decode is neither
retried nor remembered. #51198's shape — decoded pixels surviving the node — is the specific risk in a cache we
key ourselves.

**Our coverage.** [#15](https://github.com/react-native-linux/react-native-linux/issues/15) (closed),
[#44](https://github.com/react-native-linux/react-native-linux/issues/44) (open, P1: fractional-scale selection,
`resizeMode` golden matrix, load lifecycle). **Gap:** decode memory and cache lifetime under unmount. One new
issue; #44 gets an amendment note for the rounded-content-box case.

---

## Group 10 — `ScrollView`: event cadence, programmatic scroll, sticky content, recycled state

| Issue | State | Signal | Symptom | Platform |
| --- | --- | --- | --- | --- |
| [#51763](https://github.com/facebook/react-native/issues/51763) | **open** | 21r / 42c | Sticky headers became **non-touchable** in 0.79.2 | both |
| [#51290](https://github.com/facebook/react-native/issues/51290) | **open** | 16r / 20c | Touchables in a horizontal ScrollView nested in a sticky header stop responding | both |
| [#34327](https://github.com/facebook/react-native/issues/34327) | closed, `Resolution: Fixed` | 5r / 5c | Fabric emits `onScroll` for a `scrollTo` to the **same** offset; Paper did not — feedback loop | iOS |
| [#55090](https://github.com/facebook/react-native/issues/55090) | closed | 3r / 6c | `contentInset`/`contentOffset` not reset when a ScrollView is **recycled** | iOS |
| [#41034](https://github.com/facebook/react-native/issues/41034) | **open** | 11r / 10c | Fast scroll to the top yields **negative** offsets | Android |
| [#48822](https://github.com/facebook/react-native/issues/48822) | **open** | 11r / 17c | A ScrollView inside a Modal is not scrollable on first mount | Android |
| [#38730](https://github.com/facebook/react-native/issues/38730) | **open** | 9r / 25c | A ScrollView inside `position:absolute` does not scroll | Android |
| [#42874](https://github.com/facebook/react-native/issues/42874) | closed | 2r / 22c | Odd behaviour when the content is barely taller than the viewport | both |
| [#46592](https://github.com/facebook/react-native/issues/46592) | closed | — / 14c | Nested row ScrollViews never fire `onMomentumScrollEnd` | iOS |
| [#35575](https://github.com/facebook/react-native/issues/35575) | closed | 7r / 4c | `scrollTo` does nothing | iOS |
| [#38907](https://github.com/facebook/react-native/issues/38907) | **open** | 13r / 0c | `VirtualizedList` scrolls infinitely when an item is added | both |
| [#36766](https://github.com/facebook/react-native/issues/36766) / [#39421](https://github.com/facebook/react-native/issues/39421) / [#41163](https://github.com/facebook/react-native/issues/41163) | **open** | 11r+9r+7r | Lists stop rendering items past `initialNumToRender` / with `initialScrollIndex` | both |
| [#38470](https://github.com/facebook/react-native/issues/38470) | **open** | 5r / 24c | Scrolling a list whose content is moving stutters or blocks | Android |
| [#52757](https://github.com/facebook/react-native/issues/52757) | closed | 2r / 6c | `maintainVisibleContentPosition` scrolls when an item is removed from the head | both |

**Cause pattern.** Confirming the macOS finding with upstream's own numbers: `ScrollView` is a contract, not a
component, and the loudest bugs are in the parts that are *not* the physics. #34327 is the sharpest artifact in
this whole study — a landed fix whose entire content is "do not emit `onScroll` when the offset did not change",
because a library (Reanimated) built a feedback loop on the other behaviour. Sticky headers (#51763, #51290) are
Group 1 again: content translated for stickiness, hit path reading the untranslated frame.

**Why this reaches Linux.** Fully, and our docs already scope most of it as deferrals with owners:
`scrollEventThrottle` ignored, no `onScrollBeginDrag`/`onScrollEndDrag`, no `contentInset`, no
`maintainVisibleContentPosition`, `scrollTo` arriving at an empty `dispatchCommand`, no sticky-header support, no
indicator chrome. Two Linux specifics remain: `wl_pointer.axis_source` splits discrete from kinetic input, and
there is no system scrollbar to inherit.

**Our coverage.** [#16](https://github.com/react-native-linux/react-native-linux/issues/16) (closed, physics),
[#45](https://github.com/react-native-linux/react-native-linux/issues/45) (open, P0 `onScroll` cadence),
[#46](https://github.com/react-native-linux/react-native-linux/issues/46), [#47](https://github.com/react-native-linux/react-native-linux/issues/47),
[#48](https://github.com/react-native-linux/react-native-linux/issues/48), [#49](https://github.com/react-native-linux/react-native-linux/issues/49).
**Gap:** programmatic scroll as an event source (#34327 is a free specification), and the sticky/scrolled hit
region. Two new issues; #45 gets an amendment note.

---

## Group 11 — Yoga edges: out-of-flow boxes, percentages, ratios, wrapping

| Issue | State | Signal | Symptom | Platform |
| --- | --- | --- | --- | --- |
| [#52349](https://github.com/facebook/react-native/issues/52349) | **open** | 19r / 53c | Crash: `YogaLayoutableShadowNode.cpp` assertion `YGNodeGetOwner(childYogaNode) == &yogaNode_` | both |
| [#46392](https://github.com/facebook/react-native/issues/46392) | **open** | 1r / 13c | With a padded parent, an absolutely positioned child's **percentage** width/height resolves against the wrong box | both |
| [#43206](https://github.com/facebook/react-native/issues/43206) | closed | — / 14c | Same, stated as "parent padding makes absolute positioning inaccurate" | both |
| [#54174](https://github.com/facebook/react-native/issues/54174) | **open** | 1r / 11c | `position:absolute` behaves **differently** between old and new architecture in 0.82 | both |
| [#57304](https://github.com/facebook/react-native/issues/57304) | **open** | 2r / 1c | `aspectRatio` ignores a `maxWidth` constraint | both |
| [#48527](https://github.com/facebook/react-native/issues/48527) | **open** | 2r / 16c | `flexWrap` wraps unexpectedly with `alignItems:'flex-end'` | both |
| [#49984](https://github.com/facebook/react-native/issues/49984) | **open** | — / 6c | `alignContent:'stretch'` does not stretch rows in a wrapping container | both |
| [#35351](https://github.com/facebook/react-native/issues/35351) | closed | 2r / 19c | `gap` + `flexWrap` + `alignContent` renders the wrong size | both |
| [#36024](https://github.com/facebook/react-native/issues/36024) | closed | — / 2c | Flexbox `gap` does nothing on a `<ScrollView>` | both |
| [#47979](https://github.com/facebook/react-native/issues/47979) | closed, `Impact: Regression` | — / 9c | `flexBasis` does not reflect an updated state value | both |
| [#51351](https://github.com/facebook/react-native/issues/51351) | **open** | 5r / 0c | An absolute element rendered outside its parent is marked "not visible to the user" | Android |
| [#32943](https://github.com/facebook/react-native/issues/32943) | closed | — / 5c | `onLayout` width/height computed incorrectly | both |

**Cause pattern.** Yoga itself is shared C++ and mostly right; the bugs cluster where **React Native decides what
to hand Yoga**: which box a percentage resolves against when the parent has padding and the child is out of flow
(#46392, #43206), whether the constraint order is ratio-then-max or max-then-ratio (#57304), and whether a Yoga
node's owner is still the node that adopted it after a tree mutation (#52349). #54174 is the load-bearing one for
us: the *same* Yoga producing *different* absolute-position results on the two architectures means the divergence
is in the shadow-node layer, which is the layer we vendor.

**Why this reaches Linux.** Fully, with a twist that cuts in our favour and a risk that does not. In our favour:
we run Yoga on the commit thread through upstream's own `YogaLayoutableShadowNode`, so a fix upstream lands for us
on the next bump and these cases are *conformance* checks, not implementations. The risk: we are the only consumer
whose measure callback is SkParagraph, so any measure-callback contract violation (returning a different size for
the same constraints, or mutating during layout) surfaces first as a Yoga assertion like #52349 — and we have no
test that runs layout under adversarial tree mutation.

**Our coverage.** [#11](https://github.com/react-native-linux/react-native-linux/issues/11) (closed, Yoga on the
commit thread), [#73](https://github.com/react-native-linux/react-native-linux/issues/73) (open, non-finite values
at the boundary). **Gap:** no layout conformance suite at all — the largest single hole this study found. Three
new issues.

---

## Group 12 — RTL is a runtime mode, not a boot constant

| Issue | State | Signal | Symptom | Platform |
| --- | --- | --- | --- | --- |
| [#45661](https://github.com/facebook/react-native/issues/45661) | closed, `Resolution: Fixed` | 11r / **63c** | RTL layout direction does not update after a reload on the new architecture | both |
| [#48311](https://github.com/facebook/react-native/issues/48311) | closed | 7r / 29c | Switching RTL↔LTR requires terminating the app | both |
| [#47583](https://github.com/facebook/react-native/issues/47583) | closed | 4r / 6c | Same, iOS, new architecture | iOS |
| [#49451](https://github.com/facebook/react-native/issues/49451) | **open** | 1r / 15c | RTL→LTR does not work in 0.77 until a manual refresh | both |
| [#51193](https://github.com/facebook/react-native/issues/51193) | closed | 7r / 3c | `borderTopEndRadius`/`borderBottomEndRadius` behave inconsistently under RTL | Android |
| [#55768](https://github.com/facebook/react-native/issues/55768) | closed | 2r / 2c | A recycled ScrollView is offset to the **left** of the window under RTL | iOS |
| [#55433](https://github.com/facebook/react-native/issues/55433) | closed | 2r / 4c | `FlatList` renders faultily with `direction:'ltr'` inside an RTL app | both |
| [#35341](https://github.com/facebook/react-native/issues/35341) | closed | 6r / 3c | Inverted `FlatList` + RTL: poor performance and crashes | both |
| [#34314](https://github.com/facebook/react-native/issues/34314) | closed | 2r / 7c | Wrong scroll positions under RTL | Android |
| [#33423](https://github.com/facebook/react-native/issues/33423) | closed | 4r / 17c | No API to force LTR | both |
| [#51647](https://github.com/facebook/react-native/issues/51647) / [#33923](https://github.com/facebook/react-native/issues/33923) | closed | — | `I18nManager.isRTL` incorrect; constants not updated | both |
| [#54434](https://github.com/facebook/react-native/issues/54434) | closed | 2r / 11c | Arabic ligatures break across nested `<Text>` fragments | Android |

**Cause pattern.** `I18nManager.forceRTL` was designed as a boot-time flag that requires a restart, and the New
Architecture broke even the restart path (#45661, 63 comments, fixed). Underneath the process question sit three
*geometry* questions that never got separate issues: which edge `start`/`end` resolve to (#51193), where a scroll
container's origin is (#55768, #34314), and whether a subtree may override the app direction (#55433).

**Why this reaches Linux.** Fully. `docs/cpp-toolchain.md` states plainly that RTL is not handled: the paragraph
direction is hardcoded left-to-right and `writingDirection` is unimplemented, though ICU is present and
SkParagraph resolves bidi *within* a paragraph. Yoga's `YGDirection` is a per-node input we currently never set.
And our scroll controller clamps `[0, content − viewport]` on each axis with no notion of an RTL origin. On a
desktop Linux target the failure mode is worse than on mobile: there is no OS-level app restart to hide behind.

**Our coverage.** [#72](https://github.com/react-native-linux/react-native-linux/issues/72) (open, P1, from the
Windows batch) owns `writingDirection`, base paragraph direction and `I18nManager` without a bundle reload — the
*text* half. **Gap:** the *layout* half — `YGDirection`, logical edges and radii, scroll origin. One new issue.

---

## Group 13 — Animated: value ownership, node lifetime, and frame cost

| Issue | State | Signal | Symptom | Platform |
| --- | --- | --- | --- | --- |
| [#33375](https://github.com/facebook/react-native/issues/33375) | closed, `Resolution: Fixed` | 35r / **156c** | Random crash: "Animated node with tag N does not exist" | Android |
| [#37267](https://github.com/facebook/react-native/issues/37267) | closed | 22r / **145c** | Same, child variant, three years later | Android |
| [#33686](https://github.com/facebook/react-native/issues/33686) | closed | 9r / 50c | Same again | Android |
| [#51621](https://github.com/facebook/react-native/issues/51621) | **open** | 28r / 5c | `onPress` dead while animating — the shadow tree only re-syncs at animation **end** | both |
| [#50716](https://github.com/facebook/react-native/issues/50716) | closed, `Resolution: Fixed` | 20r / 18c | Animation is **20× slower** on the new architecture | iOS |
| [#44514](https://github.com/facebook/react-native/issues/44514) | closed | 3r / 6c | `useNativeDriver` performance regression in 0.74 new arch | both |
| [#34665](https://github.com/facebook/react-native/issues/34665) | closed | 9r / 22c | `Animated.Value` **resets styles** when detached | both |
| [#34795](https://github.com/facebook/react-native/issues/34795) | closed | 6r / 3c | `Animated.loop` resets to the initial value before every iteration | both |
| [#50496](https://github.com/facebook/react-native/issues/50496) | **open** | 4r / 27c | `Animated.event` + native driver leaves the wrong translate when a scroll is interrupted | Android |
| [#52657](https://github.com/facebook/react-native/issues/52657) | **open** | — / 7c | `Animated.event` does not deliver the latest `contentOffset` with the native driver | Android |
| [#36125](https://github.com/facebook/react-native/issues/36125) | closed | 1r / 17c | `Animated.event` does not fire at all with the native driver | Android |
| [#49719](https://github.com/facebook/react-native/issues/49719) | closed | 4r / 16c | Listeners never fire on values built with `Animated` operators | both |
| [#40973](https://github.com/facebook/react-native/issues/40973) | closed | — / 4c | "Style property `marginLeft` is not supported by the native animated module" | both |
| [#36608](https://github.com/facebook/react-native/issues/36608) | closed | — / 5c | `interpolate` yields `NaN` for radian outputs | both |
| [#48860](https://github.com/facebook/react-native/issues/48860) | closed | 3r / 3c | Memory leak in the Animated API | both |
| [#49838](https://github.com/facebook/react-native/issues/49838) | closed | 2r / 18c | 0.77: Animated makes some views disappear | Android |
| [#47617](https://github.com/facebook/react-native/issues/47617) | **open** | 26r / 14c | `LayoutAnimation.configureNext()` broken in 0.76 | both |
| [#38661](https://github.com/facebook/react-native/issues/38661) | **open** | 7r / 28c | "[New Architecture] LayoutAnimation breaking" | both |
| [#46568](https://github.com/facebook/react-native/issues/46568) | closed | 2r / 20c | LayoutAnimations crash under the iOS 18 SDK on the new architecture | iOS |
| [#49958](https://github.com/facebook/react-native/issues/49958) | closed | — / 6c | `LayoutAnimationController` leaks memory | Android |
| [#50442](https://github.com/facebook/react-native/issues/50442) | closed, `Resolution: Fixed` | 18r / 27c | A Modal's children render in the **top-left corner** for the first few frames | Android |

**Cause pattern.** Three failures, each with 100+ comments behind it. **Lifetime**: the animated node graph and
the mounting layer have independent lifetimes and the graph is addressed by integer tags, so a node can be
connected after its view is gone — 300 comments across #33375/#37267/#33686 and still recurring. **Ownership**:
during an animation the authoritative value lives in the driver, not the shadow tree, and everything that reads
the shadow tree (hit-testing #51621, `Animated.event` feedback #50496/#52657, detach #34665) sees a stale one.
**Cost**: a 20× regression (#50716) shipped and was found by a user, not by CI.

**Why this reaches Linux.** Fully, and this is our M2 in one table. ADR-0001 decision 6 commits us to React
Native's shared C++ animated node graph driven from a **platform-owned frame thread** — the same two-lifetime,
tag-addressed configuration that produced #33375, with our own thread as the third participant. #50442 (first
frames at the wrong position) is the mount-time analogue and maps directly onto our first-frame concerns in #46
and #59. LayoutAnimation is the one entry that may not reach us at all: it is a legacy API that the New
Architecture has been visibly failing to carry for three years (#38661 open since 2023), and "decline it
explicitly" is a legitimate outcome — but it must be a decision, not an omission.

**Our coverage.** [#19](https://github.com/react-native-linux/react-native-linux/issues/19) (open, the feature),
[#74](https://github.com/react-native-linux/react-native-linux/issues/74) (open, P0: an animation targeting an
unmounted view must not hang the frame thread — the RNW-derived twin of #33375),
[#75](https://github.com/react-native-linux/react-native-linux/issues/75) (open, Animated value conformance),
[#20](https://github.com/react-native-linux/react-native-linux/issues/20) (pacing),
[#59](https://github.com/react-native-linux/react-native-linux/issues/59) (frame-source liveness). **Gap:** value
ownership during a running animation, the native-driver style allowlist as an enforced boundary, a frame-cost gate,
and a LayoutAnimation decision. Four new issues; #74 and #75 get amendment notes.

---

## Group 14 — The commit and mounting pipeline: the code we vendor

| Issue | State | Signal | Symptom | Platform |
| --- | --- | --- | --- | --- |
| [#49077](https://github.com/facebook/react-native/issues/49077) | **open** | **49r** / 85c | "Unable to find viewState for tag N. Surface stopped: false" | both |
| [#49694](https://github.com/facebook/react-native/issues/49694) | closed | **55r** / 17c | A C++ state update is **not received** when a path-cloning commit hook is used | both |
| [#51870](https://github.com/facebook/react-native/issues/51870) | closed | 7r / 6c | `[Starvation] Assertion failed: (attempts < 1024)` in `ShadowTree.cpp` commit | both |
| [#52373](https://github.com/facebook/react-native/issues/52373) | **open** | 8r / 3c | The intermediate state of `useLayoutEffect` can be mounted by a commit from a **different thread** | both |
| [#44111](https://github.com/facebook/react-native/issues/44111) | closed, `Resolution: Fixed` | 14r / 6c | UI updates made from a layout effect are flushed in a **separate** transaction — a visible intermediate frame | both |
| [#52349](https://github.com/facebook/react-native/issues/52349) | **open** | 19r / 53c | Yoga node-owner assertion during layout | both |
| [#47576](https://github.com/facebook/react-native/issues/47576) | closed | 1r / 3c | Native view **commands** are timed incorrectly relative to mounting | iOS |
| [#55090](https://github.com/facebook/react-native/issues/55090) | closed | 3r / 6c | Recycled ScrollView keeps the previous node's `contentOffset`/`contentInset` | iOS |
| [#53050](https://github.com/facebook/react-native/issues/53050) | **open** | 17r / 19c | Recycled `TextInput` keeps the previous node's editable state | iOS |
| [#55768](https://github.com/facebook/react-native/issues/55768) | closed | 2r / 2c | Recycled ScrollView keeps the previous node's RTL offset | iOS |
| [#51869](https://github.com/facebook/react-native/issues/51869) | closed, `Resolution: Fixed` | 44r / 27c | ~1 ms of mounting cost **per view** | Android |
| [#57198](https://github.com/facebook/react-native/issues/57198) | **open** | 11r / 0c | Memory not reclaimed after unmounting a screen, across repeated navigations | both |
| [#41699](https://github.com/facebook/react-native/issues/41699) | closed | — / 4c | A `ComponentDescriptor::adopt` signature change broke every third-party component | both |

**Cause pattern.** These are not platform bugs — they are bugs in `ReactCommon`, reported against iOS and Android
because those are the only places it runs. Four sub-patterns: **termination** (a commit that retries until an
assertion, #51870), **atomicity** (an effect's intermediate state mounted as its own transaction, #44111, #52373),
**identity** (a tag whose view state has gone, #49077; a Yoga child whose owner has changed, #52349), and
**reuse** (state surviving recycling, #55090/#53050/#55768).

**Why this reaches Linux.** This group reaches us *by linkage*, not by analogy — `ShadowTree.cpp` and
`YogaLayoutableShadowNode.cpp` are compiled into `rnl_window` today. Everything above is a bug we can already
have. Three specifics: our commit runs on a thread we own, so #52373's cross-thread mounting is our default rather
than an edge case; `LinuxMountingManager::dispatchCommand` is empty, so #47576's ordering question is unanswered
rather than answered wrongly; and although we do not recycle *views*, `RetainedScene` reuses *nodes* across
mounting transactions and caches paragraphs and decoded images against them, which is the same reuse hazard with a
different noun.

**Our coverage.** [#10](https://github.com/react-native-linux/react-native-linux/issues/10) (closed, Fabric
bootstrap), [#11](https://github.com/react-native-linux/react-native-linux/issues/11) (closed, Yoga on the commit
thread), [#12](https://github.com/react-native-linux/react-native-linux/issues/12) (closed, retained scene and
damage), [#58](https://github.com/react-native-linux/react-native-linux/issues/58) (open, P0: upstream-parity
conformance suite per RN minor — the drift oracle),
[#77](https://github.com/react-native-linux/react-native-linux/issues/77) (open, cross-thread stress). **Gap:** no
test asserts that a commit terminates, that a transaction is atomic, or that a reused scene node carries nothing
from its previous life. Three new issues.

---

## Cross-cutting, briefer

**Modal and overlay surfaces** — [#50442](https://github.com/facebook/react-native/issues/50442) (children at the
top-left for the first frames, fixed), [#48611](https://github.com/facebook/react-native/issues/48611) (17r,
modal-after-modal), [#47694](https://github.com/facebook/react-native/issues/47694) (10r/44c, behaviour changed in
0.76), [#50152](https://github.com/facebook/react-native/issues/50152) (an invisible layer blocks the UI),
[#33652](https://github.com/facebook/react-native/issues/33652) ("Unimplemented component: `<ModalHostView>`" under
early Fabric), [#48822](https://github.com/facebook/react-native/issues/48822). *Applies:* the **first-frame** and
**stale-overlay-in-the-hit-path** halves apply directly to our
[#62](https://github.com/react-native-linux/react-native-linux/issues/62); the `UIViewController` presentation
half does not.

**Window dimensions** — [#41918](https://github.com/facebook/react-native/issues/41918) (5r/20c,
`useWindowDimensions` wrong with translucent system UI), [#33735](https://github.com/facebook/react-native/issues/33735)
(wrong height in fullscreen landscape). *Applies:* same class as macOS #2296, already owned by
[#50](https://github.com/react-native-linux/react-native-linux/issues/50) and
[#51](https://github.com/react-native-linux/react-native-linux/issues/51).

**Fonts** — [#54934](https://github.com/facebook/react-native/issues/54934) (23r, font weight wrong in 0.83 with a
custom font), [#47656](https://github.com/facebook/react-native/issues/47656), [#50137](https://github.com/facebook/react-native/issues/50137)
(a `TextInput` placeholder ignoring the family/weight), [#56309](https://github.com/facebook/react-native/issues/56309)
(custom fonts invisible in an app extension under Fabric). *Applies:* directly to
[#70](https://github.com/react-native-linux/react-native-linux/issues/70) (font asset registration) — synthetic
weight vs. a real weighted face is our fontconfig question too.

**IME** — [#55257](https://github.com/facebook/react-native/issues/55257) (20r, "Japanese market blocker: missing
IME composition underline", regression). *Applies:* our pre-edit rendering is exactly this, deferred from
[#26](https://github.com/react-native-linux/react-native-linux/issues/26) to
[#17](https://github.com/react-native-linux/react-native-linux/issues/17). Recorded so the styling of the
composing run is not treated as cosmetic — for one market it is a blocker.

**Accessibility geometry** — [#51351](https://github.com/facebook/react-native/issues/51351) (an out-of-parent
absolute element reported as not visible), [#54187](https://github.com/facebook/react-native/issues/54187)
(`hitSlop` does not reach the AT layer), [#36224](https://github.com/facebook/react-native/issues/36224).
*Applies:* the AT tree is a **third** geometry consumer after paint and hit, which sharpens
[#61](https://github.com/react-native-linux/react-native-linux/issues/61) and
[#91](https://github.com/react-native-linux/react-native-linux/issues/91).

---

## Group size summary

| # | Group | Proposed issues | Core issues cited | Applies to us |
| --- | --- | --- | --- | --- |
| 1 | Hit-testing vs animated/scrolled geometry | 2 | 13 | fully — sharpened by our own frame thread |
| 2 | The border box as one clip | 3 | 18 | fully — three of these are our documented deviations |
| 3 | Shadows, effects, composition order | 1 | 10 | fully — unbuilt |
| 4 | Raster scale and rasterization cost | 3 | 8 | fully — canvas-specific |
| 5 | Text vertical metrics | 1 | 12 | fully |
| 6 | Text box sizing vs shaped paragraph | 1 | 12 | fully |
| 7 | Nested `<Text>` and fragments | 1 | 10 | fully |
| 8 | `TextInput` content size and recycling | 1 | 14 | partially — the IME half is already owned |
| 9 | `Image` arithmetic, memory, lifecycle | 1 | 12 | fully |
| 10 | `ScrollView` cadence, sticky, recycled state | 2 | 14 | fully |
| 11 | Yoga edges | 3 | 12 | fully — as conformance, not implementation |
| 12 | RTL as a runtime mode | 1 | 12 | fully — layout half unowned |
| 13 | Animated and LayoutAnimation | 4 | 21 | fully — this is M2 |
| 14 | Commit and mounting pipeline | 3 | 13 | **by linkage** — this is vendored code |
| — | Modal / dimensions / fonts / IME / a11y geometry | 0 | 17 | already owned; amendments only |

Totals: **30 proposed issues**, ~188 citations over ~150 distinct upstream issues (several are cited in more than
one group — #51621 anchors Groups 1 and 13, #55090/#53050 anchor 8/10 and 14, #49442 anchors 2 and 4).

---

## Mapping to our epic, and amendments to existing issues

Existing issues in the repository as of 2026-09-02: **96, of which 69 open**, almost all sub-issues of
[#1](https://github.com/react-native-linux/react-native-linux/issues/1). This batch adds 30.

| Milestone | Existing | This batch adds |
| --- | --- | --- |
| M0 | 13 | 0 |
| M1 (renderer / text / layout / input parity) | 38 | 21 |
| M2 (animation, pacing, threading) | 12 | 9 |
| M3–M5 | 29 | 0 |
| unassigned | 4 | 0 |

One overlap needs a coordinator decision rather than a silent choice.
[#95](https://github.com/react-native-linux/react-native-linux/issues/95) is an open *plan* issue for the whole
animation program — core `Animated` with `useNativeDriver`, the worklets runtime, and Reanimated 4 — placed under
M4 and producing its own `scripts/issue-plans/animation.json`. The four `test(animation)` issues and the
`perf(animation)` gate in this batch are conformance tests for phase 1 of that program, and are written to be
either (a) rolled out now under M2 beside #19/#74/#75, or (b) folded into #95's plan when it lands. They are
scoped as tests against a named contract, so either placement works without rewriting them; what must not happen
is both.

The distribution is the finding: upstream's Fabric-era tracker is **almost entirely M1 and M2**. It has nothing to
say about our CLI, packaging or ecosystem work — those were the Windows study's contribution — and it has far more
to say about pixels, boxes and frames than either fork did, because upstream is where the components actually get
used at scale.

**Amendments to existing issues** — cases where an existing issue already owns the theme and should absorb the new
evidence rather than be duplicated. Each is a comment to add, not a new issue:

| Issue | Amendment |
| --- | --- |
| [#35](https://github.com/react-native-linux/react-native-linux/issues/35) hit vs paint | Add the *static-tree only* scope note: the animated case and the scrolled/sticky case are the first two issues in this batch. Cite [#51621](https://github.com/facebook/react-native/issues/51621), [#54988](https://github.com/facebook/react-native/issues/54988). |
| [#41](https://github.com/react-native-linux/react-native-linux/issues/41) measured == painted | Note that the equality must include **vertical** metrics; the whole of Group 5 is the failure mode. Cite [#53286](https://github.com/facebook/react-native/issues/53286), [#49886](https://github.com/facebook/react-native/issues/49886). |
| [#43](https://github.com/react-native-linux/react-native-linux/issues/43) nested `<Text>` | Add the shaping guarantee: a run must not be split at a fragment boundary. Cite [#54434](https://github.com/facebook/react-native/issues/54434). |
| [#44](https://github.com/react-native-linux/react-native-linux/issues/44) image scale/resizeMode | Add the rounded-content-box cases to the golden matrix. Cite [#33862](https://github.com/facebook/react-native/issues/33862), [#34073](https://github.com/facebook/react-native/issues/34073), [#40711](https://github.com/facebook/react-native/issues/40711). |
| [#45](https://github.com/react-native-linux/react-native-linux/issues/45) `onScroll` cadence | Add the landed upstream rule: a programmatic scroll to an unchanged offset emits **no** `onScroll`. Cite [#34327](https://github.com/facebook/react-native/issues/34327). |
| [#64](https://github.com/react-native-linux/react-native-linux/issues/64) `pointerEvents` | Add the new-arch disparity case. Cite [#54271](https://github.com/facebook/react-native/issues/54271), [#33022](https://github.com/facebook/react-native/issues/33022), [#50465](https://github.com/facebook/react-native/issues/50465). |
| [#67](https://github.com/react-native-linux/react-native-linux/issues/67) `boxShadow` | Composition order against transforms and clips is part of the definition of done, not a follow-up. Cite [#50775](https://github.com/facebook/react-native/issues/50775), [#54612](https://github.com/facebook/react-native/issues/54612). |
| [#69](https://github.com/react-native-linux/react-native-linux/issues/69) prop coverage | Cite the two upstream umbrellas as the *source of truth* for the growing surface: [#34425](https://github.com/facebook/react-native/issues/34425), [#34424](https://github.com/facebook/react-native/issues/34424). |
| [#70](https://github.com/react-native-linux/react-native-linux/issues/70) font assets | Add synthetic vs. real weight. Cite [#54934](https://github.com/facebook/react-native/issues/54934), [#47656](https://github.com/facebook/react-native/issues/47656). |
| [#72](https://github.com/react-native-linux/react-native-linux/issues/72) RTL text | Scope it explicitly to the **text** half; the layout half is the `test(layout): RTL layout` issue in this batch. Cite [#45661](https://github.com/facebook/react-native/issues/45661). |
| [#73](https://github.com/react-native-linux/react-native-linux/issues/73) non-finite values | [#57780](https://github.com/facebook/react-native/issues/57780) is the perfect reproduction: an *unset* prop became NaN, reached the clip, and blanked every child while the a11y tree still reported them. |
| [#74](https://github.com/react-native-linux/react-native-linux/issues/74) unmounted animation target | Add upstream's own 300-comment history of the identical bug: [#33375](https://github.com/facebook/react-native/issues/33375), [#37267](https://github.com/facebook/react-native/issues/37267), [#33686](https://github.com/facebook/react-native/issues/33686). |
| [#75](https://github.com/react-native-linux/react-native-linux/issues/75) Animated conformance | Add detach/loop/operator-listener semantics. Cite [#34665](https://github.com/facebook/react-native/issues/34665), [#34795](https://github.com/facebook/react-native/issues/34795), [#49719](https://github.com/facebook/react-native/issues/49719). |
| [#76](https://github.com/react-native-linux/react-native-linux/issues/76) leak gate | Add Animated and decoded-image lifetimes. Cite [#48860](https://github.com/facebook/react-native/issues/48860), [#51198](https://github.com/facebook/react-native/issues/51198), [#57198](https://github.com/facebook/react-native/issues/57198). |
| [#62](https://github.com/react-native-linux/react-native-linux/issues/62) Modal | The first-frame position and the invisible-overlay-in-the-hit-path are the two acceptance criteria. Cite [#50442](https://github.com/facebook/react-native/issues/50442), [#50152](https://github.com/facebook/react-native/issues/50152). |

---

## Proposed issues

Full bodies live in `scripts/issue-plans/core.json`. Priority, milestone, and the upstream evidence each cites:

| Title | Priority | Milestone | Core sources |
| --- | --- | --- | --- |
| test(renderer): hit-testing during a running animation — the pressed node is the painted node | P0 | M2 | #51621, #36504, #44768, #35612, #36710, #44643 |
| test(input): touchables inside sticky, scrolled and out-of-flow subtrees | P1 | M1 | #51763, #51290, #38730, #34542, #37181, #33229 |
| test(renderer): one rounded box — fill, ring, clip, content and hit region derive from it | P1 | M1 | #33862, #34073, #38124, #48078, #51489, #49442, #34553 |
| test(renderer): border painting matrix — per-side colour, transparent edges, hairlines, seams | P1 | M1 | #34722, #33950, #37954, #58054, #39286, #49606, #51193 |
| test(renderer): borderStyle dashed and dotted — implement it or refuse it in writing | P3 | M1 | #32918, #54956, #48078, #51658, #42289, #45368 |
| test(renderer): shadow, transform and clip composition order | P1 | M1 | #34320, #50775, #54612, #54556, #47920, #52703, #37078 |
| test(renderer): raster scale under nested and inverse transforms | P1 | M1 | #48673, #49442 |
| test(renderer): transformOrigin and perspective against the CSS reference | P2 | M1 | #49286, #47467 |
| test(renderer): opacity is a group property — a translucent subtree composites once | P2 | M1 | #54612, #53343, #34553 |
| perf(renderer): a mounting transaction has an allocation ceiling | P1 | M2 | #56980, #51869, #57198 |
| test(renderer): scene-node reuse carries nothing from the previous node | P0 | M1 | #55090, #53050, #55768, #48790 |
| test(renderer): decoded image lifetime — a bitmap dies with its node | P1 | M1 | #51198, #51333, #57198, #35706, #46095 |
| test(renderer): programmatic scroll must not feed itself | P1 | M1 | #34327, #35575, #41034, #42874, #46592, #52757 |
| test(text): vertical metrics — lineHeight, half-leading, ascent and descent | P0 | M1 | #53286, #56402, #53450, #49886, #39145, #53307, #33704 |
| test(text): the text box fits the shaped paragraph, and onTextLayout proves it | P1 | M1 | #54571, #55468, #54182, #52941, #54552, #37902, #37926 |
| test(text): nested fragment inheritance and shaping across fragment boundaries | P2 | M1 | #54434, #33418, #33431, #45925, #53092, #53343 |
| test(text): font scale is a runtime input, not a boot constant | P2 | M1 | #47499, #47522, #45655, #52642 |
| test(text): TextInput content size, auto-grow and update ordering | P1 | M1 | #54570, #52854, #46813, #46207, #54304, #35951 |
| test(layout): measure() and onLayout report layout geometry, not paint geometry | P1 | M1 | #54988, #32943, #51351 |
| test(layout): out-of-flow boxes against a padded parent, and percentage resolution | P1 | M1 | #46392, #43206, #54174, #34542 |
| test(layout): aspectRatio against min and max constraints | P2 | M1 | #57304, #35858 |
| test(layout): wrapping — flexWrap, alignContent, gap | P2 | M1 | #48527, #49984, #35351, #36024 |
| test(layout): RTL layout — YGDirection, logical edges and radii, scroll origin | P1 | M1 | #51193, #55768, #34314, #55433, #45661 |
| test(layout): the measure callback contract under adversarial tree mutation | P1 | M1 | #52349, #51870, #47979 |
| test(animation): the animated value and the shadow tree agree on every frame | P0 | M2 | #51621, #34665, #50496, #52657 |
| test(animation): the native-driver style allowlist is enforced at the boundary | P2 | M2 | #40973, #36608, #34022, #37900, #37061, #49719 |
| test(animation): LayoutAnimation — decide it, then prove it or refuse it | P2 | M2 | #47617, #38661, #46568, #33740, #48722, #49958 |
| perf(animation): a native-driver frame-cost gate | P1 | M2 | #50716, #44514, #47490, #34583, #38470 |
| test(core): every commit terminates, and every transaction is atomic | P0 | M1 | #51870, #52373, #44111, #49077, #49694 |
| perf(core): per-view mounting cost, measured | P1 | M2 | #51869, #47490, #36296, #57198 |

None duplicates an existing sub-issue. Where an existing issue owns the feature, the new one is scoped to
verification and names it in *Related* (#11, #12, #13, #14, #15, #16, #17, #19, #20, #35, #41, #43, #44, #45,
#46, #47, #49, #53, #58, #59, #62, #64, #67, #69, #70, #72, #73, #74, #75, #76, #77).

---

## Corpus and sampling

Two query batteries, 121 queries, run against
`repo:react/react-native is:issue created:>=2022-01-01`, each sorted by `reactions` (or `comments` where noted)
and read to a depth of 8–10 results. **341 distinct issues** were surfaced and read at title/state/signal level;
16 were opened in full to confirm a cause or a landed fix (#57780, #51621, #54988, #34327, #49286, #48673,
#50716, #47617, #33375, #34722, #55090, #53050, #49886, #51763, #56980, #52349).

**Battery 1 — 71 title-scoped and label-scoped queries.** Renderer: `borderRadius`, `borderRadius overflow`,
`borderStyle dashed dotted`, `borderWidth border color side`, `overflow hidden clip`, `boxShadow`,
`shadow clipped shadowOffset elevation`, `zIndex`, `transform origin`, `transform label:"Tech: Fabric"`,
`transform scale rotate not working`, `opacity`, `touchable outside parent`, `pointerEvents`, `hitSlop`,
`linear-gradient backgroundImage`, `filter mixBlendMode blur`, `label:"Component: View" label:"Type: New Architecture"`,
`collapsable removeClippedSubviews`. Text: `text measurement wrong`, `text cut off truncated`,
`numberOfLines ellipsizeMode`, `lineHeight`, `nested Text`, `fontFamily font not applied`, `fontWeight fontVariant`,
`label:"Component: Text"` × {`Type: New Architecture`, `Tech: Fabric`}, `allowFontScaling fontScale`. TextInput:
`label:"Component: TextInput" label:"Type: New Architecture"`, `TextInput cursor caret`,
`TextInput multiline height`, `TextInput value controlled`. Image: `resizeMode`,
`label:"Component: Image" label:"Type: New Architecture"`, `Image flicker blink`, `tintColor`,
`Image cache memory leak`. Scroll/lists: `label:"Component: ScrollView" label:"Type: New Architecture"`,
`FlatList blank white space`, `stickyHeaderIndices sticky header`, `onScroll scrollEventThrottle`,
`contentOffset scroll position jump`, `inverted FlatList`,
`label:"Component: VirtualizedList" label:"Type: New Architecture"`, `label:"Component: RefreshControl"`. Layout:
`yoga`, `flex layout wrong incorrect`, `percentage percent width height`, `aspectRatio`, `minWidth maxHeight minHeight`,
`gap rowGap columnGap`, `position absolute`, `onLayout measure incorrect`, `RTL`, `I18nManager`, `flexWrap`.
Animation: `useNativeDriver`, `label:"API: Animated" label:"Type: New Architecture"`, `label:"API: LayoutAnimation"`,
`animation flicker jump`, `reanimated new architecture`, `frame drops jank animation`, `Animated interpolate value`,
`label:"API: Transforms"`. Cross-cutting: `label:"Type: New Architecture" label:"Impact: Regression"`,
`label:"Tech: Fabric" label:"Impact: Regression"`, `label:"Type: New Architecture"`, `label:"Tech: Fabric"`,
`label:"Type: New Architecture" label:"Resolution: Fixed"`, `label:"Impact: Platform Disparity"`.

**Battery 2 — 50 full-text and comment-sorted queries.** Component labels alone, by reactions and again by
comments: `Component: View`, `Text`, `TextInput`, `Image`, `ScrollView`, `FlatList`, `VirtualizedList`, `Modal`,
`API: Animated`, `API: Transforms`, `API: LayoutAnimation`, `API: StyleSheet`. Free text: `transformOrigin`,
`transform touch area not working`, `shadowColor elevation shadow not showing`, `clipped children not visible overflow`,
`text measurement incorrect width`, `text wrap wrapping incorrect width android`,
`fontFamily fontWeight custom font not working`, `ellipsis truncat`, `baseline alignment text`, `yoga layout`,
`onLayout incorrect wrong`, `percentage height not working`, `aspectRatio`, `maxHeight minHeight not respected`,
`gap style property`, `position absolute layout wrong`, `RTL layout mirrored`,
`flex layout label:"Type: New Architecture"`, `useNativeDriver native driver animation`,
`LayoutAnimation flicker crash`, `reanimated new architecture`, `dropped frames jank scroll performance`,
`animation label:"Type: New Architecture"`, `Animated interpolate wrong value`,
`frame rate 120hz refresh rate animation`, `scroll label:"Type: New Architecture"`, `FlatList blank cells recycling`,
`stickyHeaderIndices`, `measure label:"Tech: Fabric"`, and the comment-sorted sweeps over
`Type: New Architecture`, `Tech: Fabric`, `Impact: Regression`+`New Architecture`, `Resolution: Fixed`+`New Architecture`,
`Impact: Platform Disparity`.

**What the sampling shows about the tracker itself.** Labels are applied sparsely and inconsistently:
`Tech: Fabric` has **4** issues in the whole era while `Type: New Architecture` has 704, and
`Impact: Platform Disparity` has none — so label-only sampling would have missed most of this study, and full-text
queries carried it. `Needs: Repro` outnumbers `Issue: Author Provided Repro` three to one (1,445 vs 482), which is
why the tables above weight *landed fixes* (`Resolution: Fixed`, 78 in the era) far above raw reaction counts: a
fixed issue is a specification with a known-good answer, and that is the only kind of upstream evidence that can
become a passing test on our side without a judgement call.
