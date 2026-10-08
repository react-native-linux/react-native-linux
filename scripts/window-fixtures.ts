interface WindowFixture {
  readonly bundleFileName: string | null;
  readonly extraArguments: readonly string[];
  readonly goldenFileName: string;
  readonly frameCount: string;
}

const SCREENSHOT_FRAME_COUNT = "60";
const FIRST_FRAME_COUNT = "1";

const clientDecorations = ["--app-id", "org.reactnative.linux.golden", "--force-client-decorations"];

/** Window screenshots capture renderer buffers before composition; compositor blend proof remains on #328. */
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
  {
    ...defaultFixture,
    bundleFileName: "fabric-view.js",
    extraArguments: [...defaultFixture.extraArguments, "--renderer", "raster"],
    goldenFileName: "window-raster-fabric-view.png",
  },
  {
    ...defaultFixture,
    bundleFileName: "translucent-view.js",
    extraArguments: [...defaultFixture.extraArguments, "--transparent-background", "--renderer", "raster"],
    goldenFileName: "window-raster-translucent.png",
  },
];

export { fixtures };
