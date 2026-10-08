import { afterAll, beforeAll, describe, expect, it } from "vitest";
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

/** Reads serve.ts's output, Metro's banner first, up to its listening line, and answers the server's origin. */
const readMetroOrigin = async (output: AsyncIterator<unknown>, seen: string): Promise<string> => {
  const port = metroListeningPattern.exec(seen)?.groups?.["port"] ?? null;

  if (port !== null) {
    return `http://127.0.0.1:${port}`;
  }

  const chunk = await output.next();

  if (chunk.done === true) {
    throw new Error(`Metro exited before it was listening:\n${seen}`);
  }

  return readMetroOrigin(output, seen + String(chunk.value));
};

const renderHarnessApp = (bundleUrl: string, outputPath: string): void => {
  execFileSync(binaryPath, ["--app-golden", "TestHarness", bundleUrl, outputPath], {
    stdio: ["ignore", "ignore", "inherit"],
    timeout: RENDER_PROCESS_TIMEOUT_MS,
  });
};

/**
 * #79: the test-harness app loaded by URL from a running Metro dev server rather than from a bundle file, once as a
 * production bundle and once as a `dev=true` one, which also runs LogBox, the dev-only modules and the version
 * check. `next()` alone never closes the pipe, and `resume` keeps draining it, so Metro never dies of EPIPE
 * mid-request.
 */
describe.skipIf(!existsSync(binaryPath))("application golden from a Metro dev server", () => {
  let metro: ReturnType<typeof spawn> | null = null;
  let origin = "";

  beforeAll(async () => {
    const server = spawn(execPath, [harnessServeScriptPath], { stdio: ["ignore", "pipe", "inherit"] });

    metro = server;

    // Killing Metro ends its stdout, which is what turns a server that never listens into a named failure.
    const startupDeadline = setTimeout(() => server.kill(), METRO_STARTUP_TIMEOUT_MS);

    try {
      origin = await readMetroOrigin(server.stdout[Symbol.asyncIterator](), "");
      server.stdout.resume();
    } finally {
      clearTimeout(startupDeadline);
    }
  }, METRO_STARTUP_TIMEOUT_MS);

  afterAll(() => {
    metro?.kill();
  });

  it.each(["false", "true"])(
    "renders the test-harness app served by Metro with dev=%s exactly as test-harness-app.png",
    { timeout: RENDER_TIMEOUT_MS },
    (dev) => {
      const scratchDirectory = mkdtempSync(path.join(tmpdir(), "rnl-golden-metro-"));

      try {
        const renderedPath = path.join(scratchDirectory, "app.png");

        renderHarnessApp(`${origin}/index.bundle?platform=linux&dev=${dev}&minify=false`, renderedPath);

        const rendered = PNG.sync.read(readFileSync(renderedPath));
        const golden = PNG.sync.read(readFileSync(harnessGoldenPath));

        expect(compareImages(rendered, golden)).toBeNull();
      } finally {
        rmSync(scratchDirectory, { force: true, recursive: true });
      }
    },
  );
});
