# What `react-native-macos` issues teach `react-native-linux`

- Source: `microsoft/react-native-macos`, issue tracker read-only via the GitHub search API on 2026-09-02.
- Corpus: **663 issues** total, **86 open**. Sampled with 45 themed queries (title-scoped and full-text),
  ranked by reactions and comments, plus the full open-issue tail by creation date.
- Their label set is deliberately tiny — `Area: TextInput`, `New Architecture`, `Needs: Triage`,
  `Needs: Author Feedback`, `Needs: Repro`, `Stale`, `no-recent-activity`, `Pick Request`, `Partner: Facebook`.
  Nine years of triage on one dimension. Section *Label taxonomy* proposes what to take and what to add.

## Why this repository is the right ancestor

`react-native-macos` is the only long-lived React Native platform that is simultaneously: a desktop target
(pointer, hover, keyboard focus, windows, menus), an out-of-tree platform (parallel codegen, autolinking,
Metro resolution, platform overrides), and a fork that lags upstream (currently 0.81.9 against RN 0.87.1 —
six minors, an unsupported React Native). Those are exactly our three structural risks in ADR-0001. Its bug
history is therefore a list of the mistakes available to us, already made, with reproductions attached.

Two caveats, stated so the mapping stays honest:

1. **They inherit AppKit; we inherit nothing.** Half their text, IME, accessibility and scrolling bugs are
   *integration* bugs between React Native and `NSTextView`/`NSScrollView`. On a Skia canvas the same feature
   is not an integration bug — it is unwritten code. Where that flips the lesson, it is marked below.
2. **Their fork carries the JS layer too.** Several of their loudest issues (`react-native-macos-init`,
   `pod install`) are CocoaPods-shaped and have no Linux analogue. Those are excluded rather than stretched.

---

## Group 1 — Hit-testing disagrees with painted geometry

Largest and most durable cluster in the tracker, spanning six years and every architecture.

| Issue | State | Gist |
| --- | --- | --- |
| [#914](https://github.com/microsoft/react-native-macos/issues/914) | open, 25 comments | Cursor and click go to a `TextInput` **under** an opaque overlay; z-order ignored by hit-testing |
| [#1842](https://github.com/microsoft/react-native-macos/issues/1842) | open, 4 reactions | `zIndex` does not reorder painting at all |
| [#1389](https://github.com/microsoft/react-native-macos/issues/1389) | closed | Same, earlier |
| [#2147](https://github.com/microsoft/react-native-macos/issues/2147) | open, 2 reactions | `translate` moves the pixels, not the hitbox |
| [#2063](https://github.com/microsoft/react-native-macos/issues/2063) | open | Focus ring stays at the untransformed position of a translated `TextInput` |
| [#754](https://github.com/microsoft/react-native-macos/issues/754) | closed | Right-click on selectable text only hits the top few pixels |
| [#629](https://github.com/microsoft/react-native-macos/issues/629) | closed | Clicking the scrollbar also clicks the row behind it |
| [#2935](https://github.com/microsoft/react-native-macos/issues/2935) | open | Transformed rows clip progressively while a list scrolls |
| [#2361](https://github.com/microsoft/react-native-macos/issues/2361) | open | `transformOrigin: center` rotates about the top-left; `bottom right` rotates about the centre |
| [#2038](https://github.com/microsoft/react-native-macos/issues/2038) | open, 3 reactions | `transform` stopped working entirely in 0.73 |

**Cause pattern.** There are always two geometry pipelines — one that decides which pixel a node paints into,
one that decides which node owns a screen point — and nothing forces them to agree. Every feature that changes
paint geometry (`transform`, `transformOrigin`, `zIndex`/`orderIndex`, `overflow: hidden`, scroll offset) has
to be applied to both, and a platform only ever remembers one at a time. The bug is silent: the picture is
right, the click is wrong.

**Applies to Linux/Wayland/Skia: fully, and with a sharper edge.** `docs/cpp-toolchain.md` records that hit
testing is upstream's (`PointerEventsProcessor`) while transform composition, clip stacks and paint order are
ours in `RetainedScene`. That is *literally* the two-pipeline shape. Our `zIndex` story is currently "nothing
to implement, Fabric stable-sorts by `orderIndex`" — true for paint, unproven for hit order. Our transform is
reduced to a 2D affine before painting, which upstream's hit-test does not do.

**Our coverage.** [#13](https://github.com/react-native-linux/react-native-linux/issues/13) (closed) painted the
props; [#18](https://github.com/react-native-linux/react-native-linux/issues/18) (closed) built the pointer
pipeline. Neither asserts that the node a point hits is the node whose pixels are visible there. **Gap.**

---

## Group 2 — Hover chain: semantics, availability, and cost

| Issue | State | Gist |
| --- | --- | --- |
| [#1516](https://github.com/microsoft/react-native-macos/issues/1516) | closed | "Hover Events Missing" |
| [#871](https://github.com/microsoft/react-native-macos/issues/871) / [#588](https://github.com/microsoft/react-native-macos/issues/588) | closed | `onHoverIn`/`onHoverOut` added, then silently dropped by an RN 0.62 bump |
| [#2313](https://github.com/microsoft/react-native-macos/issues/2313) | open, 7 comments | Hover **regressed again** on Fabric/0.76 after working on the old architecture |
| [#1861](https://github.com/microsoft/react-native-macos/issues/1861) | closed, 8 comments | Hover is "too slow to be used in a list" — latency, not correctness |
| [#1804](https://github.com/microsoft/react-native-macos/issues/1804) / [#926](https://github.com/microsoft/react-native-macos/issues/926) / [#525](https://github.com/microsoft/react-native-macos/issues/525) | closed | `cursor` prop unimplemented, then wrong for `editable={false}` |
| [#2781](https://github.com/microsoft/react-native-macos/issues/2781) | closed | "Keyboard and Mouse events on views not working" |

**Cause pattern.** Hover is desktop-only, so upstream's mobile-first CI never exercises it, and it is
re-broken at every architecture migration. Separately, hover is the highest-frequency event a desktop app
produces: correctness alone is not the bar, per-move cost is.

**Applies: fully.** We already feed `PointerEventsProcessor` and observe `topPointerOver`/`topPointerEnter`
in `--inject-pointer`. What is missing is the sibling/z-order/clipped-ancestor matrix, the cursor shape
(`wl_pointer.set_cursor`, which on Wayland is *our* call and must use hit order), and a cost budget.

**Our coverage.** #18 closed. **Gap:** conformance matrix + cursor shape + per-move budget.

---

## Group 3 — There is no focus model in React Native core

| Issue | State | Gist |
| --- | --- | --- |
| [#518](https://github.com/microsoft/react-native-macos/issues/518) / [#500](https://github.com/microsoft/react-native-macos/issues/500) | closed, 2 reactions | Add `focusable`, `.focus()`, `.blur()`, `onFocus`, `onBlur`, `isFocused()` to `Pressable` |
| [#483](https://github.com/microsoft/react-native-macos/issues/483) / [#795](https://github.com/microsoft/react-native-macos/issues/795) | closed, 10 comments | `enableFocusRing` has no effect; disabling the ring impossible |
| [#2954](https://github.com/microsoft/react-native-macos/issues/2954) | open, 2026-05 | Focus ring re-enables itself after a re-mount |
| [#1007](https://github.com/microsoft/react-native-macos/issues/1007) | closed, 2 reactions | Focus must not depend on the OS "full keyboard access" setting |
| [#999](https://github.com/microsoft/react-native-macos/issues/999) | open, 8 comments | `TextInput` never loses focus when you click empty background |
| [#1622](https://github.com/microsoft/react-native-macos/issues/1622) | closed | Return and Space do not trigger `onPress` on a focused control |
| [#1655](https://github.com/microsoft/react-native-macos/issues/1655) / [#1412](https://github.com/microsoft/react-native-macos/issues/1412) / [#243](https://github.com/microsoft/react-native-macos/issues/243) / [#272](https://github.com/microsoft/react-native-macos/issues/272) | closed | Six years of upstreaming focus/blur, disabled-is-not-focusable, focus-just-after-mount |
| [#913](https://github.com/microsoft/react-native-macos/issues/913) | open | `.blur()` on a `TextInput` does nothing |

**Cause pattern.** React Native's cross-platform surface has no focus concept — `BaseViewEventEmitter` has
`onFocus`/`onBlur` and nothing else. Every desktop platform invents `focusable`, a traversal order, a focus
ring, and activation keys, and then spends years keeping the four consistent with each other and with mount /
unmount / re-mount.

**Applies: fully, and it is our single largest unbuilt subsystem on the M1 path.** `docs/cpp-toolchain.md`
states it plainly: no `focusable` prop, no focus ring, no Tab handling, `onFocus`/`onBlur` never called, keys
dispatched to whatever the pointer is over. That placeholder is *exactly* the state macOS was in at #272.

**Our coverage.** #18 mentions macOS tab order as a goal and closed without it. **Gap, P0.**

---

## Group 4 — Keyboard events, repeat, shortcuts

| Issue | State | Gist |
| --- | --- | --- |
| [#823](https://github.com/microsoft/react-native-macos/issues/823) | closed, 16 comments | `onKey*` events silently stopped firing across a version bump |
| [#930](https://github.com/microsoft/react-native-macos/issues/930) / [#2781](https://github.com/microsoft/react-native-macos/issues/2781) | closed | "Keyboard events not working" — twice, four years apart |
| [#437](https://github.com/microsoft/react-native-macos/issues/437) | closed | `onKeyPress` only supports a subset of keys |
| [#702](https://github.com/microsoft/react-native-macos/issues/702) | closed | Key event payload inconsistent with `react-native-windows` |
| [#966](https://github.com/microsoft/react-native-macos/issues/966) / [#2220](https://github.com/microsoft/react-native-macos/issues/2220) | closed / open, 3 reactions | Global (window-level) keyboard listening has no API |
| [#2075](https://github.com/microsoft/react-native-macos/issues/2075) | closed, 6 comments | Cmd+C/V/A do nothing inside `TextInput` |
| [#1082](https://github.com/microsoft/react-native-macos/issues/1082) | open | `blurOnSubmit={false}` swallows Enter entirely — no `onSubmitEditing`, no `onKeyPress` |
| [#683](https://github.com/microsoft/react-native-macos/issues/683) | closed | NSBeep on every keystroke — unhandled key events escaping to the system |

**Cause pattern.** Two-sided. There is no Fabric key-event contract, so each platform invents its payload and
then diverges from its sibling platform; and key *routing* (who gets the key, what happens to keys nobody
consumed, which keys the platform reserves) is decided by the platform and never written down.

**Applies: fully.** We dispatch `keyDown`/`keyUp` with a `{key, ctrlKey, shiftKey, altKey, metaKey}` payload to
the node under the pointer — a placeholder by our own documentation. `wl_keyboard.repeat_info` is accepted and
ignored, so a held key produces one event; `xkb_compose_state` (dead keys, `dead_acute` + `e` → `é`) is
unimplemented and is a keyboard feature that IME composition does not supply.

**Our coverage.** #18 closed, #26 covers `zwp_text_input_v3` only. **Gap.**

---

## Group 5 — `TextInput` is the single largest bug generator

79 issues mention it. It is the only component with its own label in their repo.

| Issue | State | Gist |
| --- | --- | --- |
| [#2955](https://github.com/microsoft/react-native-macos/issues/2955) | closed, 2026-05 | "TextInput multiline is completely broken in new arch" |
| [#2905](https://github.com/microsoft/react-native-macos/issues/2905) | open | Multiline does not scroll under Fabric |
| [#2066](https://github.com/microsoft/react-native-macos/issues/2066) | open | Controlled `value` / `.clear()` / `setNativeProps` do not update the UI |
| [#2303](https://github.com/microsoft/react-native-macos/issues/2303) | open | Pasting into a single-line input inserts line feeds |
| [#2312](https://github.com/microsoft/react-native-macos/issues/2312) | open, 6 comments | `typingAttributes` — styles lost mid-typing |
| [#1395](https://github.com/microsoft/react-native-macos/issues/1395) | closed | Caret height wrong at large font sizes |
| [#1921](https://github.com/microsoft/react-native-macos/issues/1921) / [#2127](https://github.com/microsoft/react-native-macos/issues/2127) | closed | Caret jumps to end / jumps on multiline |
| [#1096](https://github.com/microsoft/react-native-macos/issues/1096) | closed | Selection highlight and caret colour wrong |
| [#423](https://github.com/microsoft/react-native-macos/issues/423) | closed, 1 reaction | `secureTextEntry` text readable |
| [#480](https://github.com/microsoft/react-native-macos/issues/480) / [#486](https://github.com/microsoft/react-native-macos/issues/486) / [#432](https://github.com/microsoft/react-native-macos/issues/432) / [#357](https://github.com/microsoft/react-native-macos/issues/357) / [#2270](https://github.com/microsoft/react-native-macos/issues/2270) | closed / open | Crash on backspace past a buffer size, crash on losing focus, crash on gaining focus |
| [#2738](https://github.com/microsoft/react-native-macos/issues/2738) | closed | Fabric `TextInput` renders no border |
| [#447](https://github.com/microsoft/react-native-macos/issues/447) | closed | Single-line inputs ignore most text styles |

**Cause pattern.** A text field is a state machine — buffer, selection, composition, undo stack, scroll offset,
typing attributes — duplicated between the native editor and React's controlled `value`. Every divergence in
that duplication is a bug, and the *reconciliation* direction (who wins when both changed) is where the crashes
live.

**Applies: fully, and harder.** macOS gets the editor from `NSTextView` and only has to reconcile. We have no
editor: the buffer, the selection model, the caret geometry, the scroll offset, the undo stack, the paste
normalisation and the secure-entry masking are all ours, on top of SkParagraph, plus the `zwp_text_input_v3`
serial protocol from #26. Their bug list is our specification.

**Our coverage.** [#17](https://github.com/react-native-linux/react-native-linux/issues/17) is open and is the
right container, but it has no parity matrix. **Gap:** two test-shaped sub-issues.

---

## Group 6 — IME, composition, dead keys

Thin in their tracker precisely because AppKit hands them composition. The residue is instructive:
[#683](https://github.com/microsoft/react-native-macos/issues/683) (NSBeep — the platform intercepted keys and
never told the text system), [#2312](https://github.com/microsoft/react-native-macos/issues/2312)
(typing attributes lost), [#2127](https://github.com/microsoft/react-native-macos/issues/2127) (caret jumps).

**Cause pattern.** When a platform intercepts keys to synthesise React events, it starves the input method.
The two must be ordered explicitly, not by accident.

**Applies: partially.** We already own the protocol (#26, implemented, with `TextInputV3State` at 100%
coverage), and `docs/cpp-toolchain.md` already scopes the remaining pieces to #17 (pre-edit rendering,
content hints, `set_text_change_cause`) and to `WaylandSeat` (compose sequences). The macOS lesson adds one
requirement we have not written down: **keys must not be dispatched to React while a composition is active**,
and that has to be a test.

---

## Group 7 — `ScrollView` and the lists built on it

| Issue | State | Gist |
| --- | --- | --- |
| [#1119](https://github.com/microsoft/react-native-macos/issues/1119) | open, 24 comments | `FlatList` with 500 rows of one `<Text>` is unusably slow |
| [#2965](https://github.com/microsoft/react-native-macos/issues/2965) | open, 2026-05 | `VirtualizedList`/`ScrollView` refs broken |
| [#2857](https://github.com/microsoft/react-native-macos/issues/2857) | closed, 7 comments | Large `<Text>` in a `ScrollView` has enormous phantom overflow until the window is resized |
| [#2935](https://github.com/microsoft/react-native-macos/issues/2935) | open | Transformed rows clip while scrolling |
| [#629](https://github.com/microsoft/react-native-macos/issues/629) | closed, 2 reactions | Scrollbar click passes through to the row behind |
| [#1922](https://github.com/microsoft/react-native-macos/issues/1922) | open | Horizontal scroll does not work with a mouse wheel |
| [#705](https://github.com/microsoft/react-native-macos/issues/705) | closed, 8 comments | Cannot pinch-zoom a `ScrollView` |
| [#270](https://github.com/microsoft/react-native-macos/issues/270) / [#1421](https://github.com/microsoft/react-native-macos/issues/1421) | closed | Nested scrolling; inverted `FlatList` keyboard handling |
| [#1698](https://github.com/microsoft/react-native-macos/issues/1698) | open, 2023-01 | "[Fabric] Fix RCTScrollViewComponentView" — still open |
| [#252](https://github.com/microsoft/react-native-macos/issues/252) / [#1573](https://github.com/microsoft/react-native-macos/issues/1573) | closed | Always-on scroll indicators do not adjust layout; `contentInsetAdjustmentBehavior` |

**Cause pattern.** `ScrollView` is not a component, it is a contract: content size, insets, indicator chrome
that participates in hit-testing, momentum physics, **and the cadence of `onScroll`**, which `VirtualizedList`
windowing assumes. Getting the picture right and the event cadence wrong produces "the list is slow" reports
that read like renderer bugs and are not.

**Applies: fully.** ADR-0001 already records that we get nothing free here. Two Linux-specific splits macOS
never had to make: `wl_pointer.axis_source` distinguishes a discrete wheel from a kinetic touchpad (and
`axis_stop` ends a kinetic gesture), and there is no system scrollbar — indicator chrome is ours to paint and
ours to keep out of the hit path.

**Our coverage.** [#16](https://github.com/react-native-linux/react-native-linux/issues/16) is open, owns the
physics, and is **in flight in the working tree** — `ScrollPhysics`/`ScrollController` already separate the
discrete-notch and tracked-finger paths, honour `axis_stop`, and emit `onScroll` bracketed by
`onMomentumScrollBegin`/`End`. The proposed issues are scoped around that, not over it. **Gap:**
`scrollEventThrottle` and the drag bracket, first-frame content size, indicator chrome in the hit path,
`axis_value120` and shift-wheel horizontal, and transform-under-scroll damage.

---

## Group 8 — Text measurement, invalidation, and nesting

| Issue | State | Gist |
| --- | --- | --- |
| [#2857](https://github.com/microsoft/react-native-macos/issues/2857) | closed | Measured size wrong on first render, correct after a resize forces re-measure |
| [#2607](https://github.com/microsoft/react-native-macos/issues/2607) | open | `<Text>` below a certain size **redraws forever** after a resize — measure/layout feedback loop |
| [#650](https://github.com/microsoft/react-native-macos/issues/650) | closed | `Text > View > Text` renders nothing (inline attachments) |
| [#2109](https://github.com/microsoft/react-native-macos/issues/2109) / [#2402](https://github.com/microsoft/react-native-macos/issues/2402) | closed / open | Fragment `backgroundColor` spills to the whole background |
| [#906](https://github.com/microsoft/react-native-macos/issues/906) / [#2844](https://github.com/microsoft/react-native-macos/issues/2844) | closed | `selectable` does nothing — then regressed again under Fabric |
| [#1521](https://github.com/microsoft/react-native-macos/issues/1521) | open | `transform` on `<Text>` throws |
| [#482](https://github.com/microsoft/react-native-macos/issues/482) | closed, 13 comments | "How to link fonts?" — custom font resolution is undiscoverable |
| [#1433](https://github.com/microsoft/react-native-macos/issues/1433) | closed | Platform-only text prop leaked into the shared prop type |

**Cause pattern.** Measurement and painting are two constructions of the same paragraph, and they drift. When
the drift feeds back into layout (#2607) the result is not a wrong pixel but an infinite frame loop, which on
our roadmap is a *pacing* failure, not a text failure.

**Applies: fully.** `docs/cpp-toolchain.md` records "every paint rebuilds the paragraph, and every snapshot
copies the attributed string", with a measure cache in front of the measure half only. That is precisely the
configuration that produced #2857 and #2607.

**Our coverage.** [#14](https://github.com/react-native-linux/react-native-linux/issues/14) closed with a
documented fidelity-limit list. **Gap:** an equivalence test between the measured and painted paragraph, and a
no-redraw-storm test under continuous resize.

---

## Group 9 — Image scale, resize modes, cache lifetime

| Issue | State | Gist |
| --- | --- | --- |
| [#2084](https://github.com/microsoft/react-native-macos/issues/2084) | open, 17 comments, 1 reaction | Wrong pixel-density variant chosen on first launch, right one afterwards |
| [#2498](https://github.com/microsoft/react-native-macos/issues/2498) | open | `resizeMode="contain"` distorts |
| [#316](https://github.com/microsoft/react-native-macos/issues/316) | closed | `resizeMode="cover"` wrong |
| [#1722](https://github.com/microsoft/react-native-macos/issues/1722) | open | No proper rescale when the view resizes |
| [#1948](https://github.com/microsoft/react-native-macos/issues/1948) / [#315](https://github.com/microsoft/react-native-macos/issues/315) | closed | `tintColor` ignored |
| [#921](https://github.com/microsoft/react-native-macos/issues/921) | closed | Network image unloads itself after inactivity — cache eviction visible to the user |
| [#507](https://github.com/microsoft/react-native-macos/issues/507) | closed | GIFs do not animate |

**Cause pattern.** Density selection happens before the surface knows its scale, so the first frame picks the
wrong asset; and `resizeMode` arithmetic is re-derived per platform instead of shared.

**Applies: fully, with a Wayland twist.** Our fractional scale arrives from `wp_fractional_scale_v1` *after*
the first commit and can change when the window moves between outputs — the #2084 shape, structurally
guaranteed rather than accidental. `docs/cpp-toolchain.md` already records "no `srcSet` or scale selection …
because there is no fractional scale support yet either", and no load lifecycle events.

**Our coverage.** [#15](https://github.com/react-native-linux/react-native-linux/issues/15) closed. **Gap.**

---

## Group 10 — Window identity, `Dimensions`, appearance

| Issue | State | Gist |
| --- | --- | --- |
| [#2296](https://github.com/microsoft/react-native-macos/issues/2296) | open, 2 reactions | `Dimensions.get('window')` returns `{0,0}` whenever no window has keyboard focus — root cause and fix given |
| [#2129](https://github.com/microsoft/react-native-macos/issues/2129) | open | Same, but for a popover-hosted surface |
| [#915](https://github.com/microsoft/react-native-macos/issues/915) / [#326](https://github.com/microsoft/react-native-macos/issues/326) | open / closed, 9 comments | `window` dimensions default to *screen* dimensions |
| [#322](https://github.com/microsoft/react-native-macos/issues/322) / [#1144](https://github.com/microsoft/react-native-macos/issues/1144) | closed | Layout wrong / view lags while the window is being resized |
| [#481](https://github.com/microsoft/react-native-macos/issues/481) | **open since 2020, 6 reactions, 22 comments** | `Modal` not implemented |
| [#604](https://github.com/microsoft/react-native-macos/issues/604) / [#617](https://github.com/microsoft/react-native-macos/issues/617) | closed, 5 reactions | Multiple windows / desktop window APIs — no answer |
| [#2717](https://github.com/microsoft/react-native-macos/issues/2717) / [#2830](https://github.com/microsoft/react-native-macos/issues/2830) / [#2347](https://github.com/microsoft/react-native-macos/issues/2347) | open / closed | Colours do not follow the OS theme after it changes |
| [#2736](https://github.com/microsoft/react-native-macos/issues/2736) | open | `PlatformColor` types missing under Fabric |
| [#1165](https://github.com/microsoft/react-native-macos/issues/1165) | closed | `RCTScreenScale` had to be reimplemented for macOS |

**Cause pattern.** `Dimensions` is a mobile API that assumes one screen and one window. Desktop makes "the
window" ambiguous (key window, popover, secondary window), and the platform answers with a global lookup that
returns nothing in perfectly ordinary states. Theme is the same shape: an OS-level change must invalidate a
tree that caches resolved colours.

**Applies: fully.** Wayland is *more* hostile than AppKit here: a client has no way to ask for the screen size,
`xdg_toplevel.configure` is the only size source, `wl_surface.enter`/`leave` and `wp_fractional_scale_v1` change
scale at runtime, and a window on an inactive workspace receives no frame callbacks at all (ADR-0001, decision 3).
`Modal` being unimplemented for six years is a warning about how expensive an overlay decision is on a platform
with no free window manager integration.

**Our coverage.** [#23](https://github.com/react-native-linux/react-native-linux/issues/23) lists Appearance and
Dimensions as modules, with no semantics. **Gap.**

---

## Group 11 — Out-of-tree platform tooling (the M3 minefield)

| Issue | State | Gist |
| --- | --- | --- |
| [#2778](https://github.com/microsoft/react-native-macos/issues/2778) | closed, 2025-12 | **The macOS Metro resolver never included the `.native` fallback**, so libraries silently loaded their web build. Nine years in. |
| [#53](https://github.com/microsoft/react-native-macos/issues/53) | closed, 2019 | `macos` missing from Metro's default platform list |
| [#564](https://github.com/microsoft/react-native-macos/issues/564) / [#575](https://github.com/microsoft/react-native-macos/issues/575) | closed, 20 comments | "Unable to resolve module …/AccessibilityInfo" — resolution failures presented as unrelated errors |
| [#1224](https://github.com/microsoft/react-native-macos/issues/1224) | open, 3 reactions | Autolinking opt-out uses the wrong platform key |
| [#414](https://github.com/microsoft/react-native-macos/issues/414) | closed, 13 comments | Cannot exclude an unsupported library from autolinking |
| [#1821](https://github.com/microsoft/react-native-macos/issues/1821) | closed, 3 reactions | Third-party TurboModule "cannot be found" once the new architecture is on |
| [#2742](https://github.com/microsoft/react-native-macos/issues/2742) | closed | "No component found for view with name" — codegen registration failure with no actionable message |
| [#2192](https://github.com/microsoft/react-native-macos/issues/2192) | closed | Generated modulemap missing after an upgrade |
| [#1797](https://github.com/microsoft/react-native-macos/issues/1797) / [#2304](https://github.com/microsoft/react-native-macos/issues/2304) / [#2152](https://github.com/microsoft/react-native-macos/issues/2152) / [#2743](https://github.com/microsoft/react-native-macos/issues/2743) / [#1856](https://github.com/microsoft/react-native-macos/issues/1856) | open / closed, up to 3 reactions | `react-native-macos-init` fails on fresh projects, repeatedly, across five years |
| [#2123](https://github.com/microsoft/react-native-macos/issues/2123) | closed | The platform silently overwrote the app's `resolver.resolveRequest` |

**Cause pattern.** An out-of-tree platform must reproduce **every implicit convention** the in-tree platforms
get for free, and each omission fails silently or with an error naming something else. The `.native` fallback
is the perfect specimen: no crash, no warning, just the wrong file for nine years.

**Applies: fully — this is the highest-leverage transfer in the whole study.** ADR-0001 already commits us to a
parallel codegen driver and to `react-native-platform-override`. What the macOS record adds is that the
*resolution chain itself* needs a test, that autolinking needs an opt-out key we choose deliberately, and that
a fresh-project smoke test in CI is the only thing that catches init rot.

**Our coverage.** [#21](https://github.com/react-native-linux/react-native-linux/issues/21),
[#22](https://github.com/react-native-linux/react-native-linux/issues/22),
[#29](https://github.com/react-native-linux/react-native-linux/issues/29). **Gap:** the three tests above.

---

## Group 12 — Upstream drift, and the New-Architecture regression cluster

The most important pattern in the repository, and the one that most directly validates our testing gospel.

[#2901 "Road to 0.83"](https://github.com/microsoft/react-native-macos/issues/2901) (12 reactions, the most
reacted open issue) is a tracking issue for catching up. Around it sits a cluster of regressions all filed
*after* the Fabric migration, all reported by users rather than by CI:

| Issue | Gist |
| --- | --- |
| [#2662](https://github.com/microsoft/react-native-macos/issues/2662) | Per-side `borderWidth` draws nothing under the new architecture; uniform width works |
| [#2661](https://github.com/microsoft/react-native-macos/issues/2661) / [#3079](https://github.com/microsoft/react-native-macos/issues/3079) | `useNativeDriver: true` animations never run on the new architecture |
| [#2663](https://github.com/microsoft/react-native-macos/issues/2663) | `TouchableOpacity` active state never appears |
| [#2313](https://github.com/microsoft/react-native-macos/issues/2313) | Hover events gone |
| [#2844](https://github.com/microsoft/react-native-macos/issues/2844) | `selectable` ignored |
| [#2498](https://github.com/microsoft/react-native-macos/issues/2498) | `resizeMode` broken |
| [#2738](https://github.com/microsoft/react-native-macos/issues/2738) | `TextInput` border gone |
| [#2607](https://github.com/microsoft/react-native-macos/issues/2607) | `<Text>` redraw storm |
| [#2905](https://github.com/microsoft/react-native-macos/issues/2905) / [#2955](https://github.com/microsoft/react-native-macos/issues/2955) | Multiline `TextInput` broken |
| [#2705](https://github.com/microsoft/react-native-macos/issues/2705) | A **deprecated OS timing API** stopped delivering, so all animations froze — needs a `patch-package` to run |
| [#2379](https://github.com/microsoft/react-native-macos/issues/2379) | Build breaks on an upstream C++ feature-flag member that vanished |
| [#2038](https://github.com/microsoft/react-native-macos/issues/2038) | `transform` broke in 0.73 and is still open |

**Cause pattern.** A fork that re-implements upstream behaviour has no oracle for "did this still work?".
Every upstream minor is a silent behavioural diff, and the only detector is a user. Note that the *visual*
regressions (#2662, #2738, #2844, #2498) are exactly the class a golden-image suite catches for free, and the
*timing* regressions (#2705, #3079) are exactly the class a frame-timing probe catches for free.

**Applies: fully, and it is our stated permanent condition** — ADR-0001 plans to sit three to six minors behind
React Native forever. Two concrete, cheap detectors follow: a **parity conformance suite** pinned to the RN
version we vendor and re-run on every bump, and a **frame-source liveness test** that fails when the thing that
drives our frames stops driving them (our analogue of #2705: `wl_surface.frame` callbacks stop on an inactive
workspace by design, which ADR-0001 already flags as requiring a timer fallback that nothing currently tests).

**Our coverage.** #4 (CI), #6 (golden rig), #33 (window golden), #7 (e2e driver, open),
[#20](https://github.com/react-native-linux/react-native-linux/issues/20) (pacing). **Gap:** the version-bump
oracle and the frame-source liveness assertion.

---

## Cross-cutting groups, briefer

**Menus and right-click** — [#379](https://github.com/microsoft/react-native-macos/issues/379) (right-click
fires the left-click handler), [#2085](https://github.com/microsoft/react-native-macos/issues/2085),
[#1209](https://github.com/microsoft/react-native-macos/issues/1209) (click handling dead after a context menu),
[#492](https://github.com/microsoft/react-native-macos/issues/492). *Partially applies:* Wayland has no menu bar
and no system context menu, so only the **event** half transfers — a secondary button must not be a click.

**Clipboard and drag-and-drop** — [#388](https://github.com/microsoft/react-native-macos/issues/388),
[#842](https://github.com/microsoft/react-native-macos/issues/842) (12 comments),
[#2075](https://github.com/microsoft/react-native-macos/issues/2075). *Applies:* on Wayland this is
`wl_data_device`/`wl_data_source` plus primary selection, and it is a prerequisite for a usable `TextInput`.

**Accessibility** — [#266](https://github.com/microsoft/react-native-macos/issues/266) (macOS a11y props
"need to be normalized with core"), [#1359](https://github.com/microsoft/react-native-macos/issues/1359)
(VoiceOver does not localise roles), [#1349](https://github.com/microsoft/react-native-macos/issues/1349),
[#428](https://github.com/microsoft/react-native-macos/issues/428) (`<Image>` inaccessible unless
`accessible={false}`), [#2645](https://github.com/microsoft/react-native-macos/issues/2645) (traits still
unshimmed under Fabric), [#1348](https://github.com/microsoft/react-native-macos/issues/1348). *Applies:* small
in their tracker only because AppKit does the work. For us it is #27 plus a role-mapping conformance test, and
ADR-0001 already names it a graveyard.

**Governance** — [#675 "Toxic community"](https://github.com/microsoft/react-native-macos/issues/675) is the
single most-reacted issue in the repository (14). Not a technical lesson; recorded because the loudest signal
in a small-platform tracker was about how issues were answered, not about code.

---

## Group size summary

Curated groups (issues judged in-group after reading titles; search totals in brackets are the raw query hits
and overlap across themes):

| # | Group | Curated | Raw hits | Applies to us |
| --- | --- | --- | --- | --- |
| 1 | Hit-testing vs painted geometry | 10 | — | fully |
| 2 | Hover chain and cursor | 7 | 29 | fully |
| 3 | Focus model | 13 | 69 | fully |
| 4 | Keyboard events, repeat, shortcuts | 9 | 50 | fully |
| 5 | `TextInput` | 18 | 79 | fully (harder — no editor to inherit) |
| 6 | IME / composition | 3 | — | partially (protocol done; ordering rule new) |
| 7 | `ScrollView` and lists | 12 | 53 | fully |
| 8 | Text measurement and nesting | 9 | 34 | fully |
| 9 | Image scale, resize, cache | 8 | 135 (noisy) | fully |
| 10 | Window identity, `Dimensions`, appearance | 12 | 35 | fully (Wayland is stricter) |
| 11 | Out-of-tree tooling | 12 | 39 | fully — highest leverage |
| 12 | Upstream drift / new-arch regressions | 13 | 44 | fully — our permanent condition |
| — | Menus / right-click | 4 | 26 | partially |
| — | Clipboard / drag-and-drop | 3 | 5 | fully |
| — | Accessibility | 7 | 19 | fully |

---

## Label taxonomy proposal

Their tracker proves the failure mode of a one-dimensional label set: `Area: TextInput` is the only area label
that exists, so nothing else is queryable, and triage state (`Needs: *`, `Stale`) ends up carrying all the
weight. RN core's own set is the opposite failure — dozens of overlapping labels nobody applies consistently.

Keep it **small and orthogonal**: five dimensions, one answer each, no label that could be inferred from
another. Our `area:*` set already exists and stays unchanged; the rest is new.

| Dimension | Labels | Colour | Why |
| --- | --- | --- | --- |
| `area:*` (existing) | renderer, core, input, text, testing, infra, cli, modules, a11y, packaging | as-is | Where the code lives |
| `kind:*` | bug, feat, test, parity, perf, docs | see JSON | What the work *is*. `parity` is distinct from `bug`: nothing is broken, we simply do not match iOS/Android |
| `priority:*` | P0, P1, P2, P3 | red → grey | P0 blocks the flagship; P3 is a wish |
| `platform-parity:*` | macos, windows, ios, android | one shared blue | Which platform's behaviour is the oracle for this issue |
| `origin:*` | rn-macos, rn-windows | purple | Provenance of the lesson, so a whole research batch stays auditable and can be re-reviewed when its source repo moves |
| `needs:*` | triage, decision, repro | greys/yellows | Triage state, borrowed directly from their `Needs: *` — the one part of their taxonomy that clearly worked |

Deliberately **not** adopted: `Stale`/`no-recent-activity` (a bot label that closes real bugs — #2038 and #914
are both years old and both still valid), `good first issue`/`help wanted` (premature for a one-contributor
repo), and any label duplicating milestone information (`M0`–`M5` are milestones, not labels).

`kind:parity` plus `platform-parity:*` plus `origin:*` is the combination that makes this study reusable: every
issue below is findable by the repository whose history produced it.

---

## Proposed issues

The machine-readable plan is `scripts/issue-plans/macos.json`: 29 issues, each with milestone, labels,
priority, and a body carrying the macOS evidence links, what to verify, and acceptance criteria naming the
required test layers. None duplicates an existing issue; where an existing issue owns the feature, the new one
is scoped to verification and references it by number (#13, #14, #15, #16, #17, #18, #20, #21, #22, #23, #26,
#27, #33).
