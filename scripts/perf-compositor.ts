import { existsSync, mkdirSync, mkdtempSync, rmSync } from "node:fs";
import { stopCompositor, waitForSocketName } from "./e2e-processes.ts";
import { buildEnvironment } from "./headless-compositor.ts";
import { findExecutable } from "./window-golden.ts";
import path from "node:path";
import { resolveCompositorChild } from "./e2e/grade.ts";
import { spawn } from "node:child_process";
import { tmpdir } from "node:os";
import { waitUntil } from "./e2e/wait-until.ts";

/*
 * A private headless cage at a set refresh rate, the rig scripts/e2e.ts runs rnl_window in, for a client that is
 * not cage's child: cage's child only sets the mode with wlr-randr, marks it done, and sleeps.
 */

const MODE_TIMEOUT_MS = 15_000;
const RUNTIME_DIRECTORY_MODE = 0o700;
const MODE_SET_FILE = "mode-set";
const COMPOSITOR_NAME = "cage";

interface CompositorSession {
  readonly runtimeDirectory: string;
  readonly scratchDirectory: string;
  readonly socketName: string;
}

const describeMissingCompositor = (): readonly string[] =>
  [COMPOSITOR_NAME, "wlr-randr"]
    .filter((executable) => findExecutable(executable) === null)
    .map((executable) => `${executable} is not on PATH; install the "${executable}" package`);

/** Spawned by the path `findExecutable` resolves, as scripts/e2e.ts does; `describeMissingCompositor` gates on it. */
const startCage = (runtimeDirectory: string, refreshHz: number): ReturnType<typeof spawn> =>
  spawn(
    findExecutable(COMPOSITOR_NAME) ?? COMPOSITOR_NAME,
    [
      "--",
      ...resolveCompositorChild(refreshHz, [
        "sh",
        "-c",
        'touch "$0" && exec sleep infinity',
        path.join(runtimeDirectory, MODE_SET_FILE),
      ]),
    ],
    {
      env: buildEnvironment({
        WLR_BACKENDS: "headless",
        WLR_LIBINPUT_NO_DEVICES: "1",
        WLR_RENDERER: "pixman",
        XDG_RUNTIME_DIR: runtimeDirectory,
        XKB_DEFAULT_LAYOUT: "us",
      }),
      stdio: "ignore",
    },
  );

/** The socket name, once cage's child has set the mode. */
const waitForMode = async (runtimeDirectory: string, refreshHz: number): Promise<string> => {
  const socketName = await waitForSocketName(runtimeDirectory);
  const modeSetPath = path.join(runtimeDirectory, MODE_SET_FILE);

  if (socketName === null || !(await waitUntil(() => existsSync(modeSetPath), MODE_TIMEOUT_MS))) {
    throw new Error(`cage never set a ${String(refreshHz)} Hz output in ${runtimeDirectory}`);
  }

  return socketName;
};

/**
 * Runs `body` against a cage whose output is at `refreshHz`, then stops cage and removes the scratch directory,
 * which lives under the system temporary directory because a Wayland socket path must fit in `sun_path`.
 */
const withCompositor = async <Result>(
  refreshHz: number,
  body: (session: CompositorSession) => Promise<Result>,
): Promise<Result> => {
  const scratchDirectory = mkdtempSync(path.join(tmpdir(), "rnl-perf-"));
  const runtimeDirectory = path.join(scratchDirectory, "runtime");

  mkdirSync(runtimeDirectory, { mode: RUNTIME_DIRECTORY_MODE });

  const compositor = startCage(runtimeDirectory, refreshHz);

  try {
    return await body({
      runtimeDirectory,
      scratchDirectory,
      socketName: await waitForMode(runtimeDirectory, refreshHz),
    });
  } finally {
    await stopCompositor(compositor);
    rmSync(scratchDirectory, { force: true, recursive: true });
  }
};

export { describeMissingCompositor, withCompositor };
