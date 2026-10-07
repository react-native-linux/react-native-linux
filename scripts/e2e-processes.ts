import { applyFastRefreshEdit, formatInjectorScript, gradeEditToVisible } from "./e2e/scenario.ts";
import { execPath, stdout } from "node:process";
import { readFileSync, readdirSync, writeFileSync } from "node:fs";
import { spawn, spawnSync } from "node:child_process";
import type { FastRefreshEdit } from "./e2e/fast-refresh.ts";
import { buildEnvironment } from "./window-golden.ts";
import { setTimeout as delay } from "node:timers/promises";
import path from "node:path";
import { resolveWindowFlags } from "./e2e/grade.ts";
import { waitUntil } from "./e2e/discovery.ts";

/*
 * The processes an e2e run starts: the compositor and the window inside it, rnl_inject, and the dev server of a
 * Fast Refresh scenario (#81). scripts/e2e.ts decides what runs and grades it; this file only starts and stops.
 */

const SUCCESSFUL_EXIT_STATUS = 0;
const SOCKET_TIMEOUT_MS = 15_000;
const INJECT_TIMEOUT_MS = 60_000;
const REFRESH_TIMEOUT_MS = 60_000;
const COMPOSITOR_STOP_GRACE_MS = 250;
const SOCKET_PATTERN = /^wayland-\d+$/u;
const DEV_SERVER_LISTENING_PATTERN = /metro listening on (?<port>\d+)/u;

const repositoryRoot = path.resolve(import.meta.dirname, "..");
const harnessRoot = path.join(repositoryRoot, "packages", "test-harness");
const windowBinaryPath = path.join(repositoryRoot, "build", "dev", "bin", "rnl_window");
const injectorBinaryPath = path.join(repositoryRoot, "build", "dev", "bin", "rnl_inject");
const devServerScriptPath = path.join(harnessRoot, "scripts", "serve.ts");

type Compositor = ReturnType<typeof spawn>;

interface TraceSink {
  /** Set once the compositor's stdio has closed, which is later than its exit. */
  isClosed: boolean;
  text: string;
}

interface Rig {
  readonly compositorPath: string;
  readonly lavapipeIcdPath: string;
}

/** The fields of a `Scenario` that starting and driving its processes read. */
interface LaunchedScenario {
  readonly automation: unknown;
  readonly fastRefresh: FastRefreshEdit | null;
  readonly frames: number;
  readonly injectProtocolError: boolean;
  readonly name: string;
  readonly windowFlags: readonly string[] | undefined;
}

interface WindowLaunch {
  readonly bundle: string;
  readonly frameLogPath: string;
  readonly rig: Rig;
  readonly runtimeDirectory: string;
  readonly scenario: LaunchedScenario;
  readonly screenshotPath: string;
}

/**
 * Headless wlroots and pixman need no DRM device, seat or GPU: the screenshot comes from the client's swapchain.
 * cage runs the window as its child and exits with it. Its stderr is folded into stdout so the trace keeps write
 * order (#512), and `exec` keeps cage's own exit status and signal.
 */
const startCompositor = (launch: WindowLaunch): Compositor =>
  spawn(
    "sh",
    [
      "-c",
      'exec "$0" "$@" 2>&1',
      launch.rig.compositorPath,
      "--",
      windowBinaryPath,
      "--fabric",
      launch.bundle,
      "--frames",
      String(launch.scenario.frames),
      "--screenshot",
      launch.screenshotPath,
      "--frame-log",
      launch.frameLogPath,
      ...(launch.scenario.automation === null ? [] : ["--automation"]),
      ...resolveWindowFlags(launch.scenario.windowFlags),
      ...(launch.scenario.injectProtocolError ? ["--inject-protocol-error"] : []),
    ],
    {
      env: buildEnvironment({
        VK_ICD_FILENAMES: launch.rig.lavapipeIcdPath,
        WLR_BACKENDS: "headless",
        WLR_LIBINPUT_NO_DEVICES: "1",
        WLR_RENDERER: "pixman",
        XDG_RUNTIME_DIR: launch.runtimeDirectory,
        XKB_DEFAULT_LAYOUT: "us",
      }),
      stdio: ["ignore", "pipe", "pipe"],
    },
  );

/** The event trace is the bundle's own `console.log` output, passed through by cage; the fixtures are the format. */
const attachTrace = (compositor: Compositor, sink: TraceSink): void => {
  const record = (chunk: Buffer): void => {
    sink.text += chunk.toString();
  };

  compositor.stdout?.on("data", record);
  compositor.stderr?.on("data", record);
  compositor.on("close", () => {
    sink.isClosed = true;
  });
  compositor.on("error", (error: Error) => {
    sink.text += `${error.message}\n`;
  });
};

const findSocketName = (runtimeDirectory: string): string | null =>
  readdirSync(runtimeDirectory).find((entry) => SOCKET_PATTERN.test(entry)) ?? null;

const waitForSocketName = async (runtimeDirectory: string): Promise<string | null> => {
  await waitUntil(() => findSocketName(runtimeDirectory) !== null, SOCKET_TIMEOUT_MS);

  return findSocketName(runtimeDirectory);
};

const stopCompositor = async (compositor: Compositor): Promise<void> => {
  compositor.kill("SIGTERM");
  await delay(COMPOSITOR_STOP_GRACE_MS);
  compositor.kill("SIGKILL");
};

const injectSteps = (
  steps: readonly string[],
  runtimeDirectory: string,
  socketName: string,
): { failure: string | null; status: number | null } => {
  const injection = spawnSync(injectorBinaryPath, [], {
    encoding: "utf8",
    env: buildEnvironment({
      WAYLAND_DISPLAY: socketName,
      XDG_RUNTIME_DIR: runtimeDirectory,
      XKB_DEFAULT_LAYOUT: "us",
    }),
    input: formatInjectorScript(steps),
    timeout: INJECT_TIMEOUT_MS,
  });

  const failure =
    injection.status === SUCCESSFUL_EXIT_STATUS
      ? null
      : `rnl_inject exited with status ${String(injection.status)}:\n${injection.stdout}${injection.stderr}`;
  return { failure, status: injection.status };
};

const readDevServerPort = async (output: AsyncIterator<unknown>, seen: string): Promise<string> => {
  const port = DEV_SERVER_LISTENING_PATTERN.exec(seen)?.groups?.["port"] ?? null;

  if (port !== null) {
    return port;
  }

  const chunk = await output.next();

  if (chunk.done === true) {
    throw new Error(`Metro exited before it was listening:\n${seen}`);
  }

  return readDevServerPort(output, seen + String(chunk.value));
};

/**
 * #81: a watching Metro serving `entryPath` (a file under packages/test-harness, Metro's project root) as a
 * `dev=true` bundle, requested once first so the window's fetch is not also the cold build.
 */
const startDevServer = async (entryPath: string): Promise<{ bundleUrl: string; server: Compositor }> => {
  const server = spawn(execPath, [devServerScriptPath, "--watch"], { stdio: ["ignore", "pipe", "inherit"] });
  const port = await readDevServerPort(server.stdout[Symbol.asyncIterator](), "");
  const entry = path.relative(harnessRoot, entryPath).replace(/\.tsx?$/u, "");
  const bundleUrl = `http://127.0.0.1:${port}/${entry}.bundle?platform=linux&dev=true&minify=false`;

  server.stdout.resume();
  await fetch(bundleUrl);

  return { bundleUrl, server };
};

/**
 * Runs `body` against `startDevServer`. The edited file is put back, and Metro stopped, only after `body` returns:
 * the window takes its screenshot on its last frame, and that should show the refreshed render rather than a
 * refresh back to the original.
 */
const withDevServer = async <Result>(
  entryPath: string,
  edit: FastRefreshEdit,
  body: (bundleUrl: string) => Promise<Result>,
): Promise<Result> => {
  const editedPath = path.join(repositoryRoot, edit.file);
  const original = readFileSync(editedPath, "utf8");
  const { bundleUrl, server } = await startDevServer(entryPath);

  try {
    return await body(bundleUrl);
  } finally {
    writeFileSync(editedPath, original);
    server.kill();
  }
};

/** Makes the scenario's edit and waits for the trace line it promises, recording edit-to-visible against its budget. */
const editAndAwaitRefresh = async (scenario: LaunchedScenario, trace: TraceSink): Promise<readonly string[]> => {
  const edit = scenario.fastRefresh;

  if (edit === null) {
    return [];
  }

  const filePath = path.join(repositoryRoot, edit.file);
  const startedAt = performance.now();

  writeFileSync(filePath, applyFastRefreshEdit(readFileSync(filePath, "utf8"), edit));

  const isVisible = await waitUntil(() => trace.text.includes(edit.expect), REFRESH_TIMEOUT_MS);
  const grade = gradeEditToVisible(isVisible ? Math.round(performance.now() - startedAt) : null, edit);

  stdout.write(`e2e ${scenario.name}: ${grade.note}\n`);

  return grade.failures;
};

export {
  attachTrace,
  editAndAwaitRefresh,
  injectorBinaryPath,
  injectSteps,
  startCompositor,
  stopCompositor,
  waitForSocketName,
  windowBinaryPath,
  withDevServer,
};
