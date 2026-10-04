import { describe, expect, it } from "vitest";
import { execFileSync, spawn } from "node:child_process";
import { existsSync, mkdtempSync, readFileSync, rmSync } from "node:fs";
import { PNG } from "pngjs";
import { compareImages } from "./png-diff.ts";
import { execPath } from "node:process";
import path from "node:path";
import { tmpdir } from "node:os";

const RENDER_TIMEOUT_MS = 120_000;
const METRO_STARTUP_TIMEOUT_MS = 45_000;
const RENDER_PROCESS_TIMEOUT_MS = 45_000;

const repositoryRoot = path.join(import.meta.dirname, "..", "..", "..");
const binaryPath = path.join(repositoryRoot, "build", "dev", "bin", "hello_react");
const harnessGoldenPath = path.join(import.meta.dirname, "test-harness-app.png");
const harnessServeScriptPath = path.join(repositoryRoot, "packages", "test-harness", "scripts", "serve.ts");
const metroListeningPattern = /metro listening on (?<port>\d+)/u;

/** Reads serve.ts's output, Metro's banner first, up to its listening line, and answers the app's bundle URL. */
const readMetroBundleUrl = async (output: AsyncIterator<unknown>, seen: string): Promise<string> => {
  const port = metroListeningPattern.exec(seen)?.groups?.["port"] ?? null;

  if (port !== null) {
    return `http://127.0.0.1:${port}/index.bundle?platform=linux&dev=false&minify=false`;
  }

  const chunk = await output.next();

  if (chunk.done === true) {
    throw new Error(`Metro exited before it was listening:\n${seen}`);
  }

  return readMetroBundleUrl(output, seen + String(chunk.value));
};

/**
 * #79: the test-harness app loaded by URL from a running Metro dev server rather than from a bundle file. `next()`
 * alone never closes the pipe, and `resume` keeps draining it, so Metro never dies of EPIPE mid-request.
 */
const renderHarnessAppFromMetro = async (outputPath: string): Promise<void> => {
  const metro = spawn(execPath, [harnessServeScriptPath], { stdio: ["ignore", "pipe", "inherit"] });

  // Killing Metro ends its stdout, which is what turns a server that never listens into a named failure.
  const startupDeadline = setTimeout(() => metro.kill(), METRO_STARTUP_TIMEOUT_MS);

  try {
    const bundleUrl = await readMetroBundleUrl(metro.stdout[Symbol.asyncIterator](), "");

    clearTimeout(startupDeadline);
    metro.stdout.resume();
    execFileSync(binaryPath, ["--app-golden", "TestHarness", bundleUrl, outputPath], {
      stdio: ["ignore", "ignore", "inherit"],
      timeout: RENDER_PROCESS_TIMEOUT_MS,
    });
  } finally {
    clearTimeout(startupDeadline);
    metro.kill();
  }
};

describe.skipIf(!existsSync(binaryPath))("application golden from a Metro dev server", () => {
  it(
    "renders the test-harness app served by Metro exactly as test-harness-app.png",
    { timeout: RENDER_TIMEOUT_MS },
    async () => {
      const scratchDirectory = mkdtempSync(path.join(tmpdir(), "rnl-golden-metro-"));

      try {
        const renderedPath = path.join(scratchDirectory, "app.png");

        await renderHarnessAppFromMetro(renderedPath);

        const rendered = PNG.sync.read(readFileSync(renderedPath));
        const golden = PNG.sync.read(readFileSync(harnessGoldenPath));

        expect(compareImages(rendered, golden)).toBeNull();
      } finally {
        rmSync(scratchDirectory, { force: true, recursive: true });
      }
    },
  );
});
