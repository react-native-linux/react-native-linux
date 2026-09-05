interface GoldenFixture {
  readonly bundleFileName: string;
  readonly goldenFileName: string;
  readonly renderFlag: string;
  /** Whatever the flag needs after the output path. Empty for the flags that need nothing. */
  readonly renderArguments: readonly string[];
}

export const fixtures: readonly GoldenFixture[] = [
  { bundleFileName: "fabric-view.js", goldenFileName: "fabric-view.png", renderArguments: [], renderFlag: "--golden" },
  { bundleFileName: "view-props.js", goldenFileName: "view-props.png", renderArguments: [], renderFlag: "--golden" },
  // #99: the fill, the gradient, the ring, the content clip and the child clip all cut by one rounded box.
  { bundleFileName: "rounded-box.js", goldenFileName: "rounded-box.png", renderArguments: [], renderFlag: "--golden" },
  // #100: per-side colours, transparent edges, hairline widths and the corner mitres, at three scales.
  // 700 points tall rather than the default 600: the dashed and dotted row of #101 is a sixth row of tiles.
  {
    bundleFileName: "border-matrix.js",
    goldenFileName: "border-matrix.png",
    renderArguments: ["800", "700"],
    renderFlag: "--golden",
  },
  // Text is reproducible only against the fonts scripts/vendor-fonts.ts pins into packages/core/fonts.
  // Issue #41: the flag also asserts that each box still holds the paragraph it was measured for.
  { bundleFileName: "text.js", goldenFileName: "text.png", renderArguments: [], renderFlag: "--text-fit-golden" },
  // The vertical-metrics matrix of #110: every row draws its measured frame under its painted line box.
  {
    bundleFileName: "text-metrics.js",
    goldenFileName: "text-metrics.png",
    renderArguments: [],
    renderFlag: "--text-fit-golden",
  },
  { bundleFileName: "damage.js", goldenFileName: "damage.png", renderArguments: [], renderFlag: "--damage-golden" },
  // Issue #35: this render flag is itself the assertion, and the PNG it writes is the paint side of that proof.
  {
    bundleFileName: "hit-paint.js",
    goldenFileName: "hit-paint.png",
    renderArguments: [],
    renderFlag: "--hit-paint-golden",
  },
  // Images are reproducible only against the asset packages/core/scripts/make-test-image.ts generates.
  { bundleFileName: "image.js", goldenFileName: "image.png", renderArguments: [], renderFlag: "--golden" },
  // Issue #46: the flag asserts the first frame's geometry equals the settled frame's, which is rn-macos#2857.
  {
    bundleFileName: "scroll-first-frame.js",
    goldenFileName: "scroll-first-frame.png",
    renderArguments: [],
    renderFlag: "--first-frame-golden",
  },
  // Three wheel notches is 120 points of travel; a ScrollView at rest at zero would prove nothing at all.
  {
    bundleFileName: "scroll.js",
    goldenFileName: "scroll.png",
    renderArguments: ["160", "100", "3"],
    renderFlag: "--scroll-to",
  },
  // #239: four notches is 160 points and page two starts at 150, so a carousel resting at 160 is a broken snap.
  {
    bundleFileName: "scroll-paging.js",
    goldenFileName: "scroll-paging.png",
    renderArguments: ["180", "100", "4"],
    renderFlag: "--scroll-to",
  },
  {
    bundleFileName: "focus.js",
    goldenFileName: "focus.png",
    renderArguments: ["3"],
    renderFlag: "--focus-tab",
  },
  // Issue #248: the other half of focus-visible — a click focuses the same first box Tab does, and draws no ring.
  {
    bundleFileName: "focus.js",
    goldenFileName: "focus-click.png",
    renderArguments: ["100", "120"],
    renderFlag: "--focus-click",
  },
  // Issue #248's scroll-into-view regression proof: a programmatic focus() reveals an offscreen ScrollView child.
  // Tag 14 is row4, the fifth and last row focus-scroll-list.js stacks below the viewport at rest.
  {
    bundleFileName: "focus-scroll-list.js",
    goldenFileName: "focus-command.png",
    renderArguments: ["14"],
    renderFlag: "--focus-command-golden",
  },
  // A text field draws nothing new until it is typed into: a caret and a selection are both editing state.
  {
    bundleFileName: "text-input.js",
    goldenFileName: "text-input-typing.png",
    renderArguments: ["Hello{Left}{Left}X"],
    renderFlag: "--type",
  },
  {
    bundleFileName: "text-input.js",
    goldenFileName: "text-input-selection.png",
    renderArguments: ["Hello world{Ctrl+A}"],
    renderFlag: "--type",
  },
  // Issue #53, case 3: seven Tabs reach a field four lines long in a two-line box, and its caret is below the box.
  {
    bundleFileName: "text-input.js",
    goldenFileName: "text-input-multiline-scroll.png",
    renderArguments: ["{Tab}{Tab}{Tab}{Tab}{Tab}{Tab}{Tab}{Ctrl+A}{Right}"],
    renderFlag: "--type",
  },
  // Issue #53, case 5: two Tabs reach the multiline field, so the selection spans a line break rather than a line.
  {
    bundleFileName: "text-input.js",
    goldenFileName: "text-input-multiline-selection.png",
    renderArguments: ["{Tab}{Tab}{Ctrl+A}"],
    renderFlag: "--type",
  },
  // Issue #114: one auto-growing field before anything is typed and after three lines, its sibling following it.
  {
    bundleFileName: "text-input-grow.js",
    goldenFileName: "text-input-grow-first.png",
    renderArguments: [""],
    renderFlag: "--type",
  },
  {
    bundleFileName: "text-input-grow.js",
    goldenFileName: "text-input-grow-after.png",
    renderArguments: ["one{Enter}two{Enter}three"],
    renderFlag: "--type",
  },
  // Issue #255: the caret-at-first-glyph rule, pictured under all three alignments, one focused field per golden.
  // `--type` always spends its own first Tab reaching the fixture's first field, so `""` needs no extra press.
  {
    bundleFileName: "text-input-placeholder.js",
    goldenFileName: "text-input-placeholder-left.png",
    renderArguments: [""],
    renderFlag: "--type",
  },
  // One more Tab than the left golden above reaches the center field.
  {
    bundleFileName: "text-input-placeholder.js",
    goldenFileName: "text-input-placeholder-center.png",
    renderArguments: ["{Tab}"],
    renderFlag: "--type",
  },
  // Two more Tabs than the left golden reaches the right field.
  {
    bundleFileName: "text-input-placeholder.js",
    goldenFileName: "text-input-placeholder-right.png",
    renderArguments: ["{Tab}{Tab}"],
    renderFlag: "--type",
  },
  // Issue #255's other half: one character typed, the placeholder gone, the caret after the glyph that replaced it.
  {
    bundleFileName: "text-input-placeholder.js",
    goldenFileName: "text-input-placeholder-typed.png",
    renderArguments: ["H"],
    renderFlag: "--type",
  },
  { bundleFileName: "gradient.js", goldenFileName: "gradient.png", renderArguments: [], renderFlag: "--golden" },
  // Issue #67: outset, inset, layered, per-corner, under an ancestor clip, rotated, the legacy quartet, and spread.
  { bundleFileName: "shadow.js", goldenFileName: "shadow.png", renderArguments: [], renderFlag: "--golden" },
  // Issue #102: {no transform, scale, rotate} x {no clip, self clip, ancestor clip} x {outset, inset}, tiled.
  // 700 points tall rather than the default 600: the matrix is six rows of panels a shadow has to escape.
  {
    bundleFileName: "shadow-composition.js",
    goldenFileName: "shadow-composition.png",
    renderArguments: ["800", "700"],
    renderFlag: "--golden",
  },
  // #118: one wrapping grid at two container widths — a break-point regression is a visible difference.
  { bundleFileName: "wrapping.js", goldenFileName: "wrapping.png", renderArguments: [], renderFlag: "--golden" },
  // #117: the <Image> + aspectRatio + maxWidth combination, clamped and unclamped side by side.
  {
    bundleFileName: "aspect-ratio.js",
    goldenFileName: "aspect-ratio.png",
    renderArguments: [],
    renderFlag: "--golden",
  },
  // #71: thin glyphs over opaque and transparent regions — the rasterization policy's fringing proof.
  {
    bundleFileName: "glyph-raster.js",
    goldenFileName: "glyph-raster.png",
    renderArguments: [],
    renderFlag: "--golden",
  },
  // Issue #114, item 5, and core#54304: a fixed-height multiline field's first line has to land in the same place on the first commit as it does once the run has settled.
  {
    bundleFileName: "text-input-first-frame.js",
    goldenFileName: "text-input-first-frame.png",
    renderArguments: [],
    renderFlag: "--first-frame-golden",
  },
  // Issue #114, item 4: a digits-only field's parent strips an invalid keystroke rather than adopting it, and the field's buffer falls back to what the parent actually approved, not to the value it typed.
  {
    bundleFileName: "text-input-reject.js",
    goldenFileName: "text-input-reject.png",
    renderArguments: ["3x"],
    renderFlag: "--type",
  },
  // Issue #114, item 6: a <TextInput> and a <Text> holding the same string in the same style measure to the same height — the render flag's own parity check is the assertion, and the PNG is the paint side of it.
  {
    bundleFileName: "text-input-text-height.js",
    goldenFileName: "text-input-text-height.png",
    renderArguments: [],
    renderFlag: "--text-fit-golden",
  },
];
