import { T3CODE_REVISION, buildPairingLink, findFatalErrors, readPairingUrl } from "./e2e/t3code.ts";
import { argv, env, execPath, stderr, stdout } from "node:process";
import { attachTrace, startCompositor, stopCompositor, waitForSocketName, windowBinaryPath } from "./e2e-processes.ts";
import { existsSync, mkdirSync, mkdtempSync, openSync, readFileSync, rmSync, writeFileSync } from "node:fs";
import { findExecutable, findLavapipeIcd } from "./window-golden.ts";
import { spawn, spawnSync } from "node:child_process";
import { createServer } from "node:net";
import path from "node:path";
import { tmpdir } from "node:os";
import { waitUntil } from "./e2e/discovery.ts";

/*
 * #641's bring-up fixture: T3 Code's own bundle, built for platform linux against this repository, runs in rnl_window
 * under the headless rig with a link that pairs it with a T3 server of the same checkout on loopback.
 *
 *   node scripts/t3code-fixture.ts <checkout>   (or T3CODE_ROOT=<checkout>)
 *
 * The checkout is T3 Code at T3CODE_REVISION with the mobile app's and the server's dependencies installed. The run
 * passes once startup throws nothing; until then it names where startup stopped. The main screen's accessibility tree
 * and golden join it once the screen renders.
 */

const SUCCESSFUL_EXIT_STATUS = 0;
const FAILURE_EXIT_STATUS = 1;
const UNAVAILABLE_EXIT_STATUS = 2;
const CHECKOUT_ARGUMENT_INDEX = 2;
const ANY_FREE_PORT = 0;
const EMPTY_LENGTH = 0;
const SCENARIO_NAME = "t3code-main-screen";
const FRAMES = 1200;
const SERVER_TIMEOUT_MS = 60_000;
const RUN_TIMEOUT_MS = 180_000;

const repositoryRoot = path.resolve(import.meta.dirname, "..");
const artifactsDirectory = path.join(repositoryRoot, "build", "e2e", SCENARIO_NAME);
const bundlePath = path.join(repositoryRoot, "build", "t3code", "index.linux.bundle.js");
const checkout = path.resolve(argv[CHECKOUT_ARGUMENT_INDEX] ?? env["T3CODE_ROOT"] ?? "");
const serverEntry = path.join(checkout, "apps", "server", "src", "bin.ts");
const serverLogPath = path.join(artifactsDirectory, "server.log");

type Rig = Parameters<typeof startCompositor>[number]["rig"];

const describeUnavailable = (compositorPath: string | null, lavapipeIcdPath: string | null): readonly string[] => [
  ...(existsSync(path.join(checkout, "apps", "mobile", "node_modules", "expo"))
    ? []
    : [
        `${checkout} is not an installed T3 Code checkout: clone https://github.com/pingdotgg/t3code at ${T3CODE_REVISION} and run "corepack pnpm install --frozen-lockfile --ignore-scripts --filter '@t3tools/mobile...' --filter 't3...'" in it`,
      ]),
  ...(spawnSync("git", ["-C", checkout, "rev-parse", "HEAD"], { encoding: "utf8" }).stdout.trim() === T3CODE_REVISION
    ? []
    : [`${checkout} is not at ${T3CODE_REVISION}`]),
  ...(existsSync(windowBinaryPath) ? [] : [`${windowBinaryPath} is missing; build the rnl_window target`]),
  ...(compositorPath === null ? ['cage is not on PATH; install the "cage" package'] : []),
  ...(lavapipeIcdPath === null ? ["no lavapipe ICD; install the software Vulkan driver"] : []),
];

const buildBundle = (): void => {
  const bundler = path.join(repositoryRoot, "packages", "test-harness", "scripts", "t3code-bundle.ts");
  const build = spawnSync(execPath, [bundler, checkout, bundlePath], {
    cwd: path.join(checkout, "apps", "mobile"),
    env: { ...env, NODE_ENV: "production" },
    stdio: "inherit",
  });

  if (build.status !== SUCCESSFUL_EXIT_STATUS) {
    throw new Error("Metro could not bundle T3 Code");
  }
};

const reservePort = async (): Promise<number> => {
  const probe = createServer().listen(ANY_FREE_PORT, "127.0.0.1");

  await waitUntil(() => probe.listening, SERVER_TIMEOUT_MS);

  const address = probe.address();

  probe.close();

  return typeof address === "object" && address !== null ? address.port : ANY_FREE_PORT;
};

/**
 * A T3 server of the same checkout with private state, no telemetry and no update check; resolves its stop and the
 * pairing URL it prints once it is ready.
 */
const startServer = async (
  baseDirectory: string,
): Promise<{ readonly pairingUrl: string; readonly stop: () => void }> => {
  const log = openSync(serverLogPath, "w");
  const server = spawn(
    execPath,
    [serverEntry, "serve", "--base-dir", baseDirectory, "--port", String(await reservePort()), "--host", "127.0.0.1"],
    {
      cwd: checkout,
      env: { ...env, NO_COLOR: "1", T3CODE_DISABLE_AUTO_UPDATE: "true", T3CODE_TELEMETRY_ENABLED: "false" },
      stdio: ["ignore", log, log],
    },
  );
  const readServerPairingUrl = (): string | null => readPairingUrl(readFileSync(serverLogPath, "utf8"));

  await waitUntil(() => readServerPairingUrl() !== null, SERVER_TIMEOUT_MS);

  return { pairingUrl: readServerPairingUrl() ?? "", stop: () => server.kill("SIGTERM") };
};

/** The window, handed the link as a bare argument: the activation URL `Linking.getInitialURL` answers (#363). */
const runWindow = async (link: string, rig: Rig): Promise<string> => {
  const runtimeDirectory = mkdtempSync(path.join(tmpdir(), "rnl-t3code-"));
  const trace = { isClosed: false, text: "" };
  const compositor = startCompositor({
    bundle: bundlePath,
    frameLogPath: path.join(artifactsDirectory, "frames.jsonl"),
    rig,
    runtimeDirectory,
    scenario: {
      automation: null,
      fastRefresh: null,
      frames: FRAMES,
      injectProtocolError: false,
      name: SCENARIO_NAME,
      outputRefreshHz: null,
      steadyAcrossReloads: false,
      windowFlags: ["--no-decorations", link],
    },
    screenshotPath: path.join(artifactsDirectory, "screenshot.png"),
  });

  attachTrace(compositor, trace);
  await waitForSocketName(runtimeDirectory);
  await waitUntil(() => trace.isClosed, RUN_TIMEOUT_MS);
  await stopCompositor(compositor);
  rmSync(runtimeDirectory, { force: true, recursive: true });
  writeFileSync(path.join(artifactsDirectory, "trace.log"), trace.text);

  return trace.text;
};

const reportRun = (trace: string): void => {
  const [firstFatalError = null] = findFatalErrors(trace);

  if (firstFatalError === null) {
    stdout.write(`e2e ${SCENARIO_NAME}: passed; artifacts: ${artifactsDirectory}\n`);

    return;
  }

  stderr.write(`e2e ${SCENARIO_NAME}: startup stopped at\n${firstFatalError}\nartifacts: ${artifactsDirectory}\n`);
  process.exitCode = FAILURE_EXIT_STATUS;
};

const runFixture = async (rig: Rig): Promise<void> => {
  mkdirSync(artifactsDirectory, { recursive: true });
  buildBundle();

  const baseDirectory = mkdtempSync(path.join(tmpdir(), "rnl-t3code-server-"));
  const server = await startServer(baseDirectory);

  try {
    reportRun(await runWindow(buildPairingLink(server.pairingUrl), rig));
  } finally {
    server.stop();
    rmSync(baseDirectory, { force: true, recursive: true });
  }
};

const compositorPath = findExecutable("cage");
const lavapipeIcdPath = findLavapipeIcd();
const unavailable = describeUnavailable(compositorPath, lavapipeIcdPath);

if (compositorPath === null || lavapipeIcdPath === null || unavailable.length > EMPTY_LENGTH) {
  stderr.write(unavailable.map((reason) => `${reason}\n`).join(""));
  process.exitCode = UNAVAILABLE_EXIT_STATUS;
} else {
  await runFixture({ compositorPath, lavapipeIcdPath });
}
