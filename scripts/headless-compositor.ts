import { env, pid } from "node:process";
import { existsSync, mkdtempSync, rmSync } from "node:fs";
import { setTimeout as delay } from "node:timers/promises";
import path from "node:path";
import { spawn } from "node:child_process";
import { tmpdir } from "node:os";

const SOCKET_TIMEOUT_MS = 15_000;
const SOCKET_POLL_INTERVAL_MS = 50;
const COMPOSITOR_STOP_GRACE_MS = 250;
const WINDOW_WIDTH = "800";
const WINDOW_HEIGHT = "600";

/** Every variable that could point the client at the developer's own session or driver; dropped, not overwritten. */
const overriddenEnvironmentNames = new Set([
  "DBUS_SESSION_BUS_ADDRESS",
  "DISPLAY",
  "WAYLAND_DISPLAY",
  "VK_DRIVER_FILES",
  "VK_ICD_FILENAMES",
]);

/**
 * Mesa keeps lavapipe's compiled shaders under `XDG_CACHE_HOME`, which every run points at a fresh directory, so
 * without a cache that outlives the run each window compiles them again in its first frames after mount (#704).
 */
const mesaShaderCacheDirectory = path.resolve(import.meta.dirname, "..", "build", "mesa-shader-cache");

/**
 * The window caps every fallback interval at one 60 Hz frame while a kernel thermal zone runs hot (#335), so a warm
 * runner would grade different frame timings than a cool one. A headless window reads its zones from this directory,
 * which nothing creates, and so stays nominal (#774); exporting `RNL_THERMAL_ROOT` opts a run into real or fake zones.
 */
const nominalThermalRoot = path.resolve(import.meta.dirname, "..", "build", "no-thermal-zones");

const buildEnvironment = (overrides: Record<string, string>): Record<string, string | undefined> => {
  const inherited = Object.fromEntries(
    Object.entries(env).filter(([variableName]) => !overriddenEnvironmentNames.has(variableName)),
  );

  return {
    RNL_THERMAL_ROOT: nominalThermalRoot,
    ...inherited,
    MESA_SHADER_CACHE_DIR: mesaShaderCacheDirectory,
    ...overrides,
  };
};

interface Compositor {
  readonly process: ReturnType<typeof spawn>;
  readonly readLog: () => string;
}

/** No DRM device, seat or display needed, so this runs unattended in CI; `--no-config` ignores a developer's `weston.ini`. */
const startCompositor = (compositorPath: string, runtimeDirectory: string, socketName: string): Compositor => {
  let log = "";
  const compositor = spawn(
    compositorPath,
    [
      "--backend=headless",
      "--no-config",
      `--socket=${socketName}`,
      `--width=${WINDOW_WIDTH}`,
      `--height=${WINDOW_HEIGHT}`,
      "--idle-time=0",
    ],
    { env: buildEnvironment({ XDG_RUNTIME_DIR: runtimeDirectory }), stdio: ["ignore", "pipe", "pipe"] },
  );

  const recordOutput = (chunk: Buffer): void => {
    log += chunk.toString();
  };

  compositor.stdout?.on("data", recordOutput);
  compositor.stderr?.on("data", recordOutput);
  compositor.on("error", (error: Error) => {
    log += error.message;
  });

  return { process: compositor, readLog: () => log };
};

const waitForCompositorSocket = async (compositor: Compositor, socketPath: string): Promise<void> => {
  const deadline = Date.now() + SOCKET_TIMEOUT_MS;

  while (!existsSync(socketPath)) {
    if (compositor.process.exitCode !== null || Date.now() > deadline) {
      throw new Error(`the compositor never created ${socketPath}:\n${compositor.readLog()}`);
    }

    await delay(SOCKET_POLL_INTERVAL_MS);
  }
};

const stopCompositor = async (compositor: Compositor, runtimeDirectory: string): Promise<void> => {
  compositor.process.kill("SIGTERM");
  await delay(COMPOSITOR_STOP_GRACE_MS);
  compositor.process.kill("SIGKILL");
  rmSync(runtimeDirectory, { force: true, recursive: true });
};

/**
 * A headless compositor in a fresh runtime directory for the length of `render`, stopped whatever it did. A
 * failure inside `render` carries the compositor's own output, which is where a client that never mapped explains
 * itself.
 */
const runUnderCompositor = async (
  compositorPath: string,
  lavapipeIcdPath: string,
  render: (clientEnvironment: Record<string, string | undefined>) => void,
): Promise<void> => {
  const runtimeDirectory = mkdtempSync(path.join(tmpdir(), "rnl-headless-compositor-"));
  const socketName = `rnl-headless-compositor-${String(pid)}`;
  const compositor = startCompositor(compositorPath, runtimeDirectory, socketName);

  try {
    await waitForCompositorSocket(compositor, path.join(runtimeDirectory, socketName));
    render(
      buildEnvironment({
        VK_ICD_FILENAMES: lavapipeIcdPath,
        WAYLAND_DISPLAY: socketName,
        XDG_CACHE_HOME: path.join(runtimeDirectory, "cache"),
        XDG_DATA_HOME: path.join(runtimeDirectory, "data"),
        XDG_RUNTIME_DIR: runtimeDirectory,
        XDG_STATE_HOME: path.join(runtimeDirectory, "state"),
      }),
    );
  } catch (error) {
    throw new Error(`${error instanceof Error ? error.message : String(error)}\ncompositor:\n${compositor.readLog()}`, {
      cause: error,
    });
  } finally {
    await stopCompositor(compositor, runtimeDirectory);
  }
};

const NOTIFICATION_DAEMON_TIMEOUT_MS = 10_000;
const NOTIFICATION_DAEMON_READY = "[fake-notifications] ready";
const fakeNotificationsBinaryPath = path.resolve(
  import.meta.dirname,
  "..",
  "build",
  "dev",
  "bin",
  "rnl_fake_notifications",
);

/**
 * Resolves once `isReady`, and rejects with the spawn error when `child` cannot start, or with `describeTimeout()` when
 * it is not ready within `NOTIFICATION_DAEMON_TIMEOUT_MS`.
 */
const awaitReady = async (
  child: ReturnType<typeof spawn>,
  isReady: () => boolean,
  describeTimeout: () => string,
): Promise<void> => {
  const deadline = Date.now() + NOTIFICATION_DAEMON_TIMEOUT_MS;
  const startup: { error?: Error } = {};

  child.once("error", (error) => {
    startup.error = error;
  });

  while (!isReady()) {
    if (startup.error instanceof Error) {
      throw startup.error;
    }

    if (Date.now() >= deadline) {
      throw new Error(describeTimeout());
    }

    await delay(SOCKET_POLL_INTERVAL_MS);
  }
};

const spawnSessionBus = (socketPath: string): ReturnType<typeof spawn> =>
  spawn("dbus-daemon", ["--session", "--nofork", "--nopidfile", `--address=unix:path=${socketPath}`], {
    stdio: "ignore",
  });

const spawnFakeNotificationDaemon = (socketPath: string, output: { text: string }): ReturnType<typeof spawn> => {
  const daemon = spawn(fakeNotificationsBinaryPath, [], {
    env: buildEnvironment({ DBUS_SESSION_BUS_ADDRESS: `unix:path=${socketPath}` }),
    stdio: ["ignore", "pipe", "pipe"],
  });
  const record = (chunk: Buffer): void => {
    output.text += chunk.toString();
  };

  daemon.stdout.on("data", record);
  daemon.stderr.on("data", record);

  return daemon;
};

/** Starts the bus, then the daemon on it, adding each to `children` as it spawns so the caller stops it. */
const startNotificationProcesses = async (
  socketPath: string,
  daemonOutput: { text: string },
  children: ReturnType<typeof spawn>[],
): Promise<void> => {
  const bus = spawnSessionBus(socketPath);

  children.push(bus);
  await awaitReady(
    bus,
    () => existsSync(socketPath),
    () => `dbus-daemon never created ${socketPath}`,
  );

  const daemon = spawnFakeNotificationDaemon(socketPath, daemonOutput);

  children.push(daemon);
  await awaitReady(
    daemon,
    () => daemonOutput.text.includes(NOTIFICATION_DAEMON_READY),
    () => `rnl_fake_notifications never printed "${NOTIFICATION_DAEMON_READY}"; it printed: ${daemonOutput.text}`,
  );
};

/**
 * #274: runs `body` with a private session bus at `<runtimeDirectory>/bus`, which is where a client's sd-bus looks
 * once `buildEnvironment` has dropped `DBUS_SESSION_BUS_ADDRESS`, and `rnl_fake_notifications` on it owning
 * `org.freedesktop.Notifications`. Rejects when either process fails to start or is not ready in time. The daemon's
 * lines join `trace` after `body`, as one block: two processes' pipes have no order between them, so a scenario
 * expects the daemon's lines after the window's. Both processes are stopped after `body`.
 */
const withNotificationDaemon = async <Result>(
  runtimeDirectory: string,
  trace: { text: string },
  body: () => Promise<Result>,
): Promise<Result> => {
  const socketPath = path.join(runtimeDirectory, "bus");
  const daemonOutput = { text: "" };
  const children: ReturnType<typeof spawn>[] = [];

  try {
    await startNotificationProcesses(socketPath, daemonOutput, children);

    return await body();
  } finally {
    for (const child of children) {
      child.kill();
    }

    trace.text += daemonOutput.text;
  }
};

export { buildEnvironment, fakeNotificationsBinaryPath, runUnderCompositor, withNotificationDaemon };
