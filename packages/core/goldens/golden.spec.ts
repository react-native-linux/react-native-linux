import { describe, expect, it } from "vitest";

import { env, execPath, stdout } from "node:process";
import { execFileSync, spawnSync } from "node:child_process";
import { existsSync, mkdtempSync, readFileSync, rmSync } from "node:fs";

import { PNG } from "pngjs";

import { compareImages } from "./png-diff.ts";
import { compareImagesPerceptually } from "./perceptual-diff.ts";
import path from "node:path";
import { tmpdir } from "node:os";

const RENDER_TIMEOUT_MS = 120_000;

interface GoldenFixture {
  readonly bundleFileName: string;
  readonly goldenFileName: string;
  readonly renderFlag: string;
  /** Whatever the flag needs after the output path. Empty for the flags that need nothing. */
  readonly renderArguments: readonly string[];
}

const goldensDirectory = import.meta.dirname;
const packageDirectory = path.join(goldensDirectory, "..");
const bundlesDirectory = path.join(packageDirectory, "test-bundles");
const binaryPath = path.join(packageDirectory, "..", "..", "build", "dev", "bin", "hello_react");

const isRegenerating = env["RNL_UPDATE_GOLDENS"] === "1";
const hasBinary = existsSync(binaryPath);

const fixtures: readonly GoldenFixture[] = [
  { bundleFileName: "fabric-view.js", goldenFileName: "fabric-view.png", renderArguments: [], renderFlag: "--golden" },
  { bundleFileName: "view-props.js", goldenFileName: "view-props.png", renderArguments: [], renderFlag: "--golden" },
  // Text is reproducible only against the fonts scripts/vendor-fonts.ts pins into packages/core/fonts.
  { bundleFileName: "text.js", goldenFileName: "text.png", renderArguments: [], renderFlag: "--golden" },
  { bundleFileName: "damage.js", goldenFileName: "damage.png", renderArguments: [], renderFlag: "--damage-golden" },
  // Images are reproducible only against the asset packages/core/scripts/make-test-image.ts generates.
  { bundleFileName: "image.js", goldenFileName: "image.png", renderArguments: [], renderFlag: "--golden" },
  // Three wheel notches is 120 points of travel; a ScrollView at rest at zero would prove nothing at all.
  {
    bundleFileName: "scroll.js",
    goldenFileName: "scroll.png",
    renderArguments: ["160", "100", "3"],
    renderFlag: "--scroll-to",
  },
  {
    bundleFileName: "focus.js",
    goldenFileName: "focus.png",
    renderArguments: ["3"],
    renderFlag: "--focus-tab",
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
  { bundleFileName: "gradient.js", goldenFileName: "gradient.png", renderArguments: [], renderFlag: "--golden" },
];

const renderFixture = (fixture: GoldenFixture, outputPath: string): void => {
  const bundlePath = path.join(bundlesDirectory, fixture.bundleFileName);

  execFileSync(binaryPath, [fixture.renderFlag, bundlePath, outputPath, ...fixture.renderArguments], {
    stdio: ["ignore", "ignore", "inherit"],
  });
};

const decodePng = (filePath: string): { data: Uint8Array; height: number; width: number } => {
  const decoded = PNG.sync.read(readFileSync(filePath));

  return { data: decoded.data, height: decoded.height, width: decoded.width };
};

const buildMissingGoldenMessage = (goldenPath: string): string =>
  `${goldenPath} does not exist. Regenerate it with "pnpm test:golden:update", review the image, then commit it.`;

const expectGoldenToMatch = (fixture: GoldenFixture, goldenPath: string): void => {
  const scratchDirectory = mkdtempSync(path.join(tmpdir(), "rnl-golden-"));

  try {
    const renderedPath = path.join(scratchDirectory, fixture.goldenFileName);

    renderFixture(fixture, renderedPath);

    expect(compareImages(decodePng(renderedPath), decodePng(goldenPath))).toBeNull();
  } finally {
    rmSync(scratchDirectory, { force: true, recursive: true });
  }
};

if (!hasBinary) {
  stdout.write(
    `skipping golden-image tests: ${binaryPath} is missing. Build it with "cmake --build build/dev --target hello_react".\n`,
  );
}

describe.skipIf(!hasBinary)("golden images", () => {
  for (const fixture of fixtures) {
    it(`renders ${fixture.bundleFileName} exactly as ${fixture.goldenFileName}`, { timeout: RENDER_TIMEOUT_MS }, () => {
      const goldenPath = path.join(goldensDirectory, fixture.goldenFileName);

      if (isRegenerating) {
        renderFixture(fixture, goldenPath);

        expect(existsSync(goldenPath)).toBe(true);

        return;
      }

      expect(existsSync(goldenPath), buildMissingGoldenMessage(goldenPath)).toBe(true);
      expectGoldenToMatch(fixture, goldenPath);
    });
  }
});

const UNAVAILABLE_EXIT_STATUS = 2;
const SUCCESSFUL_EXIT_STATUS = 0;

const windowRigPath = path.join(packageDirectory, "..", "..", "scripts", "window-golden.ts");
const windowRenderDirectory = path.join(packageDirectory, "..", "..", "build", "window-goldens");
const windowGoldenFileNames = ["window-fabric-view.png", "window-view-props.png"];

/**
 * One rig process renders every window fixture, because starting a compositor per image would cost more than the
 * renders do. It runs at collection time, so the compositor is gone before the first assertion, and it writes
 * straight into the goldens directory when regenerating, so nothing has to be copied afterwards.
 */
const windowRig = spawnSync(
  execPath,
  [windowRigPath, "--output", isRegenerating ? goldensDirectory : windowRenderDirectory],
  {
    encoding: "utf8",
    timeout: RENDER_TIMEOUT_MS,
  },
);

const isWindowRigUnavailable = windowRig.status === UNAVAILABLE_EXIT_STATUS;

if (isWindowRigUnavailable) {
  stdout.write(`skipping window goldens:\n${windowRig.stderr}`);
}

const buildMissingWindowGoldenMessage = (goldenPath: string): string =>
  `${goldenPath} does not exist. Regenerate it with "pnpm test:golden:window:update", review the image, then commit it.`;

describe.skipIf(isWindowRigUnavailable)("window goldens", () => {
  it("renders every fixture through the window path", () => {
    expect(windowRig.status, `${windowRig.stdout}${windowRig.stderr}`).toBe(SUCCESSFUL_EXIT_STATUS);
  });

  for (const goldenFileName of windowGoldenFileNames) {
    it(`matches ${goldenFileName} through the window path`, () => {
      const goldenPath = path.join(goldensDirectory, goldenFileName);

      expect(existsSync(goldenPath), buildMissingWindowGoldenMessage(goldenPath)).toBe(true);

      if (isRegenerating) {
        return;
      }

      const renderedPath = path.join(windowRenderDirectory, goldenFileName);

      expect(existsSync(renderedPath), `${windowRig.stdout}${windowRig.stderr}`).toBe(true);
      expect(compareImagesPerceptually(decodePng(renderedPath), decodePng(goldenPath))).toBeNull();
    });
  }
});
