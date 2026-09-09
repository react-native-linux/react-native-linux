interface WindowFixture {
  readonly bundleFileName: string | null;
  readonly extraArguments: readonly string[];
  readonly goldenFileName: string;
  readonly frameCount: string;
}

const SCREENSHOT_FRAME_COUNT = "60";
const FIRST_FRAME_COUNT = "1";

const clientDecorations = ["--app-id", "org.reactnative.linux.golden", "--force-client-decorations"];

/**
 * The first-frame fixture takes no bundle and one frame: the placeholder paints synchronously, so this is the
 * first-buffer check the invisible-window bug (#328) fails and a 60-frame settle does not. See *Surface commit
 * ordering* in docs/cpp-toolchain.md. The decorations fixture is #329's drawn bar; the translucent one runs
 * `--transparent-background`, proving #328's composite-alpha selection; the rest are bare.
 */
const defaultFixture = { extraArguments: ["--no-decorations"], frameCount: SCREENSHOT_FRAME_COUNT };

const fixtures: readonly WindowFixture[] = [
  { ...defaultFixture, bundleFileName: "fabric-view.js", goldenFileName: "window-fabric-view.png" },
  { ...defaultFixture, bundleFileName: "view-props.js", goldenFileName: "window-view-props.png" },
  { ...defaultFixture, bundleFileName: null, frameCount: FIRST_FRAME_COUNT, goldenFileName: "window-first-frame.png" },
  {
    ...defaultFixture,
    bundleFileName: "fabric-view.js",
    extraArguments: clientDecorations,
    goldenFileName: "window-decorations.png",
  },
  {
    ...defaultFixture,
    bundleFileName: "translucent-view.js",
    extraArguments: [...defaultFixture.extraArguments, "--transparent-background"],
    goldenFileName: "window-translucent.png",
  },
];

export { fixtures };
