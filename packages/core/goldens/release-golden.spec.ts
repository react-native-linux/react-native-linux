import { describe, expect, it } from "vitest";
import { execFileSync, spawnSync } from "node:child_process";
import { existsSync, mkdtempSync, readFileSync, rmSync } from "node:fs";
import { PNG } from "pngjs";
import { compareImages } from "./png-diff.ts";
import { createRequire } from "node:module";
import { execPath } from "node:process";
import path from "node:path";
import { tmpdir } from "node:os";

const RENDER_TIMEOUT_MS = 180_000;

const repositoryRoot = path.join(import.meta.dirname, "..", "..", "..");
const binaryDirectory = path.join(repositoryRoot, "build", "dev", "bin");
const binaryPath = path.join(binaryDirectory, "hello_react");
const hermesCompilerPath = path.join(binaryDirectory, "hermesc");
const harnessPackagePath = path.join(repositoryRoot, "packages", "test-harness", "package.json");
const harnessBundleScriptPath = path.join(repositoryRoot, "packages", "test-harness", "scripts", "bundle.ts");
const harnessGoldenPath = path.join(import.meta.dirname, "test-harness-app.png");
const probeSourcePath = path.join(import.meta.dirname, "..", "test-bundles", "release-throw.js");
const symbolicatorPath = createRequire(harnessPackagePath).resolve("metro-symbolicate");
const hasBinaries = existsSync(binaryPath) && existsSync(hermesCompilerPath);

const decode = (filePath: string): PNG => PNG.sync.read(readFileSync(filePath));

const withScratchDirectory = (body: (scratchDirectory: string) => void): void => {
  const scratchDirectory = mkdtempSync(path.join(tmpdir(), "rnl-release-"));

  try {
    body(scratchDirectory);
  } finally {
    rmSync(scratchDirectory, { force: true, recursive: true });
  }
};

/**
 * Bundles the test-harness app for production and compiles it to `-O` bytecode. hermesc's stderr is piped rather
 * than inherited: it warns with source excerpts of the whole minified bundle, and a failure still carries them.
 */
const renderReleaseApp = (scratchDirectory: string): string => {
  const bundlePath = path.join(scratchDirectory, "index.linux.bundle.js");
  const bytecodePath = path.join(scratchDirectory, "index.linux.hbc");
  const renderedPath = path.join(scratchDirectory, "app.png");

  execFileSync(execPath, [harnessBundleScriptPath, bundlePath, "--release"], {
    stdio: ["ignore", "ignore", "inherit"],
  });
  execFileSync(hermesCompilerPath, ["-O", "-emit-binary", "-out", bytecodePath, bundlePath], { stdio: "pipe" });
  execFileSync(binaryPath, ["--app-golden", "TestHarness", bytecodePath, renderedPath], {
    stdio: ["ignore", "ignore", "inherit"],
  });

  return renderedPath;
};

/** The probe's bytecode stack as `hello_react` prints it, run through Metro's symbolicator and the probe's map. */
const symbolicateProbe = (scratchDirectory: string): string => {
  const bytecodePath = path.join(scratchDirectory, "release-throw.hbc");

  execFileSync(hermesCompilerPath, ["-O", "-emit-binary", "-output-source-map", "-out", bytecodePath, probeSourcePath]);

  const stack = spawnSync(binaryPath, [bytecodePath], { encoding: "utf8" })
    .stderr.split("\n")
    .filter((line) => line.startsWith("    at "))
    .join("\n");

  return execFileSync(execPath, [symbolicatorPath, `${bytecodePath}.map`], { encoding: "utf8", input: stack });
};

/**
 * #82: what a shipped app runs is neither a development bundle nor its source. react-native-windows#10255 is a
 * release build that renders a white screen, so the production bundle compiled to `-O` bytecode has to render the
 * same pixels the development bundle does, and a crash in it has to be readable through its source map.
 */
describe.skipIf(!hasBinaries)("release build", () => {
  it(
    "renders the minified test-harness bundle, compiled to -O bytecode, exactly as test-harness-app.png",
    { timeout: RENDER_TIMEOUT_MS },
    () => {
      withScratchDirectory((scratchDirectory) => {
        const rendered = decode(renderReleaseApp(scratchDirectory));

        expect(compareImages(rendered, decode(harnessGoldenPath))).toBeNull();
      });
    },
  );

  it("maps a bytecode stack back to the source lines that threw", { timeout: RENDER_TIMEOUT_MS }, () => {
    withScratchDirectory((scratchDirectory) => {
      const symbolicated = symbolicateProbe(scratchDirectory);

      expect(symbolicated).toContain(`at inner (${probeSourcePath}:7:`);
      expect(symbolicated).toContain(`at outer (${probeSourcePath}:4:`);
      expect(symbolicated).toContain(`at global (${probeSourcePath}:9:`);
    });
  });
});
