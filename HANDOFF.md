# Session handoff — 2026-09-03, 14:40 Vienna

Written for whoever picks this up next. Everything below is verified against the repository and the
issue tracker at the time of writing, not recalled. It replaces the 13:40 handoff, whose "uncommitted
work in the tree" section is now history: that work is landed.

## Where things stand

`main` is at **0191c49**. Four commits landed since the last handoff and the first three are green on
all seven CI jobs — runs **33752877246**, **33754111115** and **33755226481**. Run **33755871125**
(0191c49) was still in flight when this was written; check it before building on top.

The tracker has **127 open issues**: M0 1, M1 43, M2 16, M3 18, M4 40, M5 7. New issues go under the
milestone epic (#172 M0 … #177 M5), never directly under #1, because GitHub caps an issue at 100
sub-issues.

The working tree is clean and `pnpm validate` passes.

## What landed

- **#99 and #100 (closed).** One `SceneRoundedBox` behind the fill, the ring, the `overflow: hidden`
  clip, the content clips and the hit region. A press outside a rounded corner's arc now misses the
  node. The border matrix came with it: wedge clips overlap by one point, so a mitre is a colour
  boundary rather than the hairline seam two anti-aliased wedges leave (core#33950), and a sub-pixel
  width is promoted to one device pixel so `StyleSheet.hairlineWidth` draws (core#58054). That seam
  fix is the only reason `view-props.png` changed: 144 pixels, all on the four mitre diagonals.
- **#108 (open, see below).** A decoded bitmap is now owned by the nodes drawing it and by the cache,
  and by nothing else. `SceneImageContent::pixels` carries it as the `std::shared_ptr<void>` upstream's
  `ImageResponse` uses, so the scene still links no Skia. An eviction can no longer blank a mounted
  node (react-native-macos#921, which we had). Reading a scene now settles the decode queue first, in
  `BundleRunner` rather than in the golden renderer.
- **#35 (closed).** `--hit-paint-golden`: every node in `hit-paint.js` paints a unique colour, so a
  pixel names the node that painted it; the run samples the live scene's `findNodeAtPoint` over a grid
  and the two answers have to agree wherever the picture is unambiguous.
- **#41 (pushed).** `--text-fit-golden`: every text node's paragraph is laid out at the width the
  painter uses and has to fit the box it was measured into.

## Two behaviour changes worth knowing about

- **Containment is half-open on the right and the bottom.** A box at x = 30 that is 150 wide paints
  columns 30 to 179 and leaves 180 to its neighbour; `Rect::containsPoint`, which upstream's hit test
  uses, closes both intervals and answered 180. The hit-paint proof found it on its first run.
- **`ScenePrimitive` carries its node's tag.** The painter needs no identity; a proof does.

## The two proof modes, and why they are shaped that way

`--damage-golden` was the first of these: an acceptance criterion turned into an assertion, with the
PNG as a by-product. `--hit-paint-golden` and `--text-fit-golden` are the same pattern, and both exist
because the unit gate is a **Skia-free build** — a GoogleTest suite cannot rasterise or shape anything,
so a proof that needs pixels or SkParagraph has to live in `hello_react`.

Each one has a stated precondition, and they are in `docs/cpp-toolchain.md` rather than in anyone's
head:

- hit-paint samples **pixel centres**, because on a rotated edge the corner of a fully painted pixel
  can sit outside the shape that painted it. Blended pixels are skipped and a run where fewer than
  three quarters of the samples were comparable fails, so the skip cannot turn a disagreement into a
  pass.
- text-fit cannot tell a paragraph that re-wrapped from an author who deliberately constrained a text
  box, so it runs on the auto-sized fixtures (`text.js`, `text-metrics.js`) and anything that squeezes
  a paragraph on purpose stays on plain `--golden`.

## Open question for the user

**#108** has a status comment rather than a close. Its title's contract is delivered and tested, but
four of its "what to verify" items are not, and each already has an owning issue: decode-size bounding
and `onError` are **#44**, process RSS across 50 navigations is **#76**, the cache hit-rate probe is
**#20**. Close it and let those carry the rest, or keep it as the umbrella — the user's call.

## Environment notes for a fresh worktree

These cost time to rediscover:

- A `t3` worktree has **no `node_modules` and no `third_party`**. `pnpm install --frozen-lockfile`, then
  symlink `third_party` and `packages/core/fonts` from the main checkout and add both to
  `.git/info/exclude` — a symlink is not a directory, so `.gitignore`'s trailing-slash entries do not
  match it and they show up as untracked.
- **`CC=clang CXX=clang++` is required.** GCC fails on the vendored `ConcreteShadowNode.h` with
  `-Wchanges-meaning`. CI uses clang-18; local clang 22 works.
- `cmake` and `ninja` are mise-managed with no global version set, so they are not on `PATH` through the
  shims. Use `~/.local/share/mise/installs/cmake/latest/cmake-4.4.3-linux-x86_64/bin` and
  `~/.local/share/mise/installs/ninja/latest`.
- **cage, weston and lavapipe are not installed on this machine**, so `pnpm e2e` and the window goldens
  cannot run locally; CI is the only place they do. `hello_react --inject-pointer` is the local stand-in
  for a pointer e2e and it exercises the same dispatch.

Everything else from the previous handoff still holds: jscpd runs at threshold 0 so every new test file
trips it (shared fixtures live in `packages/core/tests/SceneTestSupport.h`); oxlint's
`capitalized-comments` rule fires on **each line** of a multi-line `//` comment; oxfmt's ignore lives in
`.prettierignore`; a paint-less `<View>` is flattened before the diff; debug builds abort at exit if a
`jsi` handle outlives the runtime (#171 is the remaining one).

## Suggested next moves

1. **#45** (P0) — `scrollEventThrottle`, the `onScrollBeginDrag`/`onScrollEndDrag` bracket around the
   momentum pair, and a bounded mounted-row count over a 500-row list. The emitter wants to be a pure
   class inside the coverage gate.
2. **#53** (P0) — the TextInput parity matrix.
3. **#44** (P1) — image scale selection and the load lifecycle events, which is where two of #108's
   remaining items belong.
4. **#101** (P3) — `borderStyle` dashed and dotted: implement it on the ring with `SkDashPathEffect` or
   refuse it in writing. The ledger already says `not-implemented` rather than `deviating`.

## Where the durable notes live

`docs/cpp-toolchain.md` is the engineering ledger — every feature has a section, including the two new
proof modes and the *One rounded box* and *Border painting* sections. `docs/ecosystem/` holds the four
strategy documents. The memory file at
`~/.claude/projects/-home-vitalyiegorov-suuudokuuu/memory/react-native-linux-project.md` carries the
running status log.
