import {
  T3CODE_REVISION,
  buildPairingLink,
  describeForeignCheckout,
  gradeWindowRun,
  overlayInstallArguments,
  overlayPackArguments,
  readPairingUrl,
  tailLines,
} from "./e2e/t3code.ts";
import { argv, env, execPath, stderr, stdout } from "node:process";
import { attachTrace, startCompositor, stopCompositor, waitForSocketName, windowBinaryPath } from "./e2e-processes.ts";
import {
  closeSync,
  existsSync,
  mkdirSync,
  mkdtempSync,
  openSync,
  readFileSync,
  readdirSync,
  rmSync,
  writeFileSync,
} from "node:fs";
import { findExecutable, findLavapipeIcd } from "./window-golden.ts";
import { spawn, spawnSync } from "node:child_process";
import { embedExpoAppAssets } from "@react-native-linux/cli/expo-app-assets.ts";
import { packageAliases } from "@react-native-linux/cli/package-aliases.ts";
import path from "node:path";
import { tmpdir } from "node:os";
import { waitUntil } from "./e2e/discovery.ts";

/*
 * #641's bring-up fixture: T3 Code's own bundle, built for platform linux against this repository, runs in rnl_window
 * under the headless rig with a link that pairs it with a T3 server of the same checkout on loopback.
 *
 *   node scripts/t3code-fixture.ts <checkout>   (or T3CODE_ROOT=<checkout>)
 *
 * The checkout is the fixture's own copy of T3 Code at T3CODE_REVISION inside build/t3code, such as
 * build/t3code/checkout, with the mobile app's and the server's dependencies installed; any other path is refused. The
 * fixture packs this platform's overlay packages and adds them to the mobile app, which rewrites its package.json and
 * lockfile, so the alias table resolves them as it would for an application that installed them from a registry. The
 * run passes once the window presents a frame and exits cleanly with startup throwing nothing; until then it names
 * where startup stopped. The main screen's accessibility tree and golden join it once the screen renders.
 */

const SUCCESSFUL_EXIT_STATUS = 0;
const FAILURE_EXIT_STATUS = 1;
const UNAVAILABLE_EXIT_STATUS = 2;
const CHECKOUT_ARGUMENT_INDEX = 2;
const EMPTY_LENGTH = 0;
const SCENARIO_NAME = "t3code-main-screen";
const FRAMES = 1200;
const SERVER_TIMEOUT_MS = 60_000;
const SERVER_STOP_GRACE_MS = 5000;
const RUN_TIMEOUT_MS = 180_000;

const repositoryRoot = path.resolve(import.meta.dirname, "..");
const artifactsDirectory = path.join(repositoryRoot, "build", "e2e", SCENARIO_NAME);
const fixtureDirectory = path.join(repositoryRoot, "build", "t3code");
const bundlePath = path.join(fixtureDirectory, "index.linux.bundle.js");
const overlayDirectory = path.join(fixtureDirectory, "overlays");
const checkout = path.resolve(argv[CHECKOUT_ARGUMENT_INDEX] ?? env["T3CODE_ROOT"] ?? "");
const mobileRoot = path.join(checkout, "apps", "mobile");
const serverEntry = path.join(checkout, "apps", "server", "src", "bin.ts");
const serverLogPath = path.join(artifactsDirectory, "server.log");

type Rig = Parameters<typeof startCompositor>[number]["rig"];
type Compositor = ReturnType<typeof startCompositor>;
type WindowRun = Parameters<typeof gradeWindowRun>[number];

interface Server {
  readonly child: ReturnType<typeof spawn>;
  readonly state: { hasEnded: boolean };
}

const describeUnavailable = (compositorPath: string | null, lavapipeIcdPath: string | null): readonly string[] => [
  ...(["mobile", "server"].every((app) => existsSync(path.join(checkout, "apps", app, "node_modules")))
    ? []
    : [
        `${checkout} is not an installed T3 Code checkout: clone https://github.com/pingdotgg/t3code at ${T3CODE_REVISION} into ${path.join(fixtureDirectory, "checkout")} and run "corepack pnpm install --frozen-lockfile --ignore-scripts --filter '@t3tools/mobile...' --filter 't3...'" in it`,
      ]),
  ...(spawnSync("git", ["-C", checkout, "rev-parse", "HEAD"], { encoding: "utf8" }).stdout.trim() === T3CODE_REVISION
    ? []
    : [`${checkout} is not at ${T3CODE_REVISION}`]),
  ...(existsSync(windowBinaryPath) ? [] : [`${windowBinaryPath} is missing; build the rnl_window target`]),
  ...(compositorPath === null ? ['cage is not on PATH; install the "cage" package'] : []),
  ...(lavapipeIcdPath === null ? ["no lavapipe ICD; install the software Vulkan driver"] : []),
];

const fail = (message: string): void => {
  stderr.write(`e2e ${SCENARIO_NAME}: failed\n${message}\nartifacts: ${artifactsDirectory}\n`);
  process.exitCode = FAILURE_EXIT_STATUS;
};

/** The output of pnpm run in `directory` when it fails, or null when it succeeds. */
const runPnpm = (pnpmArguments: readonly string[], directory: string): string | null => {
  const run = spawnSync("pnpm", pnpmArguments, { cwd: directory, encoding: "utf8" });

  return run.status === SUCCESSFUL_EXIT_STATUS ? null : `${run.stdout}${run.stderr}`;
};

/** Every overlay package the alias table names, packed and added to the mobile app; the failure, or null. */
const installOverlays = (): string | null => {
  rmSync(overlayDirectory, { force: true, recursive: true });

  return (
    runPnpm(overlayPackArguments(packageAliases, overlayDirectory), repositoryRoot) ??
    runPnpm(
      overlayInstallArguments(readdirSync(overlayDirectory).map((name) => path.join(overlayDirectory, name))),
      checkout,
    )
  );
};

const buildBundle = (): boolean => {
  const bundler = path.join(repositoryRoot, "packages", "test-harness", "scripts", "t3code-bundle.ts");
  mkdirSync(path.dirname(bundlePath), { recursive: true });
  const build = spawnSync(execPath, [bundler, checkout, bundlePath], {
    cwd: mobileRoot,
    env: { ...env, NODE_ENV: "development" },
    stdio: "inherit",
  });

  return build.status === SUCCESSFUL_EXIT_STATUS;
};

/**
 * A T3 server of the same checkout with private state, no telemetry and no update check. It picks its own free port,
 * which the pairing URL it prints carries. The parent's copy of the log descriptor closes once the child holds its own.
 */
const startServer = (baseDirectory: string): Server => {
  const log = openSync(serverLogPath, "w");

  try {
    const child = spawn(execPath, [serverEntry, "serve", "--base-dir", baseDirectory, "--host", "127.0.0.1"], {
      cwd: checkout,
      env: { ...env, NO_COLOR: "1", T3CODE_DISABLE_AUTO_UPDATE: "true", T3CODE_TELEMETRY_ENABLED: "false" },
      stdio: ["ignore", log, log],
    });
    const state = { hasEnded: false };
    const end = (): void => {
      state.hasEnded = true;
    };

    child.once("exit", end);
    child.once("error", end);

    return { child, state };
  } finally {
    closeSync(log);
  }
};

const stopServer = async ({ child, state }: Server): Promise<void> => {
  child.kill("SIGTERM");

  if (!(await waitUntil(() => state.hasEnded, SERVER_STOP_GRACE_MS))) {
    child.kill("SIGKILL");

    if (!(await waitUntil(() => state.hasEnded, SERVER_STOP_GRACE_MS))) {
      fail(`the T3 server, process ${String(child.pid)}, did not exit on SIGKILL`);
    }
  }
};

const readServerLog = (): string => readFileSync(serverLogPath, "utf8");

/** The pairing URL once the server is ready; null when it ends first or prints none within its deadline. */
const waitForPairingUrl = async ({ state }: Server): Promise<string | null> => {
  await waitUntil(() => state.hasEnded || readPairingUrl(readServerLog()) !== null, SERVER_TIMEOUT_MS);

  return readPairingUrl(readServerLog());
};

const driveWindow = async (
  compositor: Compositor,
  trace: { isClosed: boolean; text: string },
  runtimeDirectory: string,
): Promise<Pick<WindowRun, "hasSocket" | "isTraceClosed">> => {
  attachTrace(compositor, trace);

  const hasSocket = (await waitForSocketName(runtimeDirectory)) !== null;
  const isTraceClosed = hasSocket && (await waitUntil(() => trace.isClosed, RUN_TIMEOUT_MS));

  return { hasSocket, isTraceClosed };
};

/** The window, handed the link as a bare argument: the activation URL `Linking.getInitialURL` answers (#363). */
const runWindow = async (link: string, rig: Rig): Promise<WindowRun> => {
  const runtimeDirectory = mkdtempSync(path.join(tmpdir(), "rnl-t3code-"));

  try {
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

    try {
      const driven = await driveWindow(compositor, trace, runtimeDirectory);

      return { ...driven, exitCode: compositor.exitCode, signal: compositor.signalCode, trace: trace.text };
    } finally {
      await stopCompositor(compositor);
      writeFileSync(path.join(artifactsDirectory, "trace.log"), trace.text);
    }
  } finally {
    rmSync(runtimeDirectory, { force: true, recursive: true });
  }
};

const reportRun = (run: WindowRun): void => {
  const failures = gradeWindowRun(run);

  if (failures.length === EMPTY_LENGTH) {
    stdout.write(`e2e ${SCENARIO_NAME}: passed; artifacts: ${artifactsDirectory}\n`);

    return;
  }

  fail(`${failures.join("\n")}\ntrace tail:\n${tailLines(run.trace)}`);
};

const runWithServer = async (server: Server, rig: Rig): Promise<void> => {
  const pairingUrl = await waitForPairingUrl(server);

  if (pairingUrl === null) {
    fail(
      `the T3 server ${server.state.hasEnded ? "ended" : `was not ready within ${String(SERVER_TIMEOUT_MS)} ms`} before it printed a pairing URL\n${serverLogPath}:\n${tailLines(readServerLog())}`,
    );

    return;
  }

  reportRun(await runWindow(buildPairingLink(pairingUrl), rig));
};

const runAgainstServer = async (rig: Rig): Promise<void> => {
  const baseDirectory = mkdtempSync(path.join(tmpdir(), "rnl-t3code-server-"));

  try {
    const server = startServer(baseDirectory);

    try {
      await runWithServer(server, rig);
    } finally {
      await stopServer(server);
    }
  } finally {
    rmSync(baseDirectory, { force: true, recursive: true });
  }
};

const runOnOwnCheckout = async (rig: Rig): Promise<void> => {
  const overlayFailure = installOverlays();

  if (overlayFailure !== null) {
    fail(`the overlay packages could not be packed and added to T3 Code\n${tailLines(overlayFailure)}`);
  } else if (buildBundle()) {
    await embedExpoAppAssets(mobileRoot, path.join(path.dirname(bundlePath), "assets"));
    await runAgainstServer(rig);
  } else {
    fail("Metro could not bundle T3 Code");
  }
};

const runFixture = async (rig: Rig): Promise<void> => {
  mkdirSync(artifactsDirectory, { recursive: true });
  mkdirSync(fixtureDirectory, { recursive: true });

  const foreignCheckout = describeForeignCheckout(checkout, fixtureDirectory);

  if (foreignCheckout === null) {
    await runOnOwnCheckout(rig);
  } else {
    fail(foreignCheckout);
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
