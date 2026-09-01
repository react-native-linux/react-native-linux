import { describe, expect, it } from "vitest";

import { env, stdout } from "node:process";
import { existsSync, mkdtempSync, readFileSync, rmSync } from "node:fs";

import { PNG } from "pngjs";

import { compareImages } from "./png-diff.ts";
import { execFileSync } from "node:child_process";
import path from "node:path";
import { tmpdir } from "node:os";

const RENDER_TIMEOUT_MS = 120_000;

interface GoldenFixture {
  readonly bundleFileName: string;
  readonly goldenFileName: string;
}

const goldensDirectory = import.meta.dirname;
const packageDirectory = path.join(goldensDirectory, "..");
const bundlesDirectory = path.join(packageDirectory, "test-bundles");
const binaryPath = path.join(packageDirectory, "..", "..", "build", "dev", "bin", "hello_react");

const isRegenerating = env["RNL_UPDATE_GOLDENS"] === "1";
const hasBinary = existsSync(binaryPath);

const fixtures: readonly GoldenFixture[] = [{ bundleFileName: "fabric-view.js", goldenFileName: "fabric-view.png" }];

const renderFixture = (fixture: GoldenFixture, outputPath: string): void => {
  execFileSync(binaryPath, ["--golden", path.join(bundlesDirectory, fixture.bundleFileName), outputPath], {
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
