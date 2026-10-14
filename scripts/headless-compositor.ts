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
const overriddenEnvironmentNames = new Set(["DISPLAY", "WAYLAND_DISPLAY", "VK_DRIVER_FILES", "VK_ICD_FILENAMES"]);

const buildEnvironment = (overrides: Record<string, string>): Record<string, string | undefined> => {
  const inherited = Object.fromEntries(
    Object.entries(env).filter(([variableName]) => !overriddenEnvironmentNames.has(variableName)),
  );

  return { ...inherited, ...overrides };
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

export { buildEnvironment, runUnderCompositor };
