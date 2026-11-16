import { setTimeout as delay } from "node:timers/promises";
import { execFile } from "node:child_process";
import { promisify } from "node:util";

const CLIPBOARD_EXPECT_PREFIX = "clipboard-expect ";
const CLIPBOARD_SET_PREFIX = "clipboard-set ";
const CLIPBOARD_TIMEOUT_MS = 10_000;
const POLL_INTERVAL_MS = 100;

interface CommandResult {
  readonly output: string;
  readonly succeeded: boolean;
}

type RunCommand = (command: string, commandArguments: readonly string[]) => Promise<CommandResult>;

const isClipboardStep = (step: string): boolean =>
  step.startsWith(CLIPBOARD_EXPECT_PREFIX) || step.startsWith(CLIPBOARD_SET_PREFIX);

const executeFile = promisify(execFile);

/** One command against the compositor `environment` names, killed at `CLIPBOARD_TIMEOUT_MS`. */
const createCommandRunner =
  (environment: Record<string, string | undefined>): RunCommand =>
  async (command, commandArguments) => {
    try {
      const { stdout } = await executeFile(command, commandArguments, {
        encoding: "utf8",
        env: environment,
        timeout: CLIPBOARD_TIMEOUT_MS,
      });

      return { output: stdout, succeeded: true };
    } catch {
      return { output: "", succeeded: false };
    }
  };

/** Retries `wl-paste` until the selection holds `expected`: the window publishes a copy on its next frame. */
const expectClipboard = async (expected: string, runCommand: RunCommand, deadline: number): Promise<string | null> => {
  const pasted = await runCommand("wl-paste", ["--no-newline"]);

  if (pasted.succeeded && pasted.output === expected) {
    return null;
  }

  if (Date.now() > deadline) {
    return `the compositor's selection held "${pasted.output}", not "${expected}"`;
  }

  await delay(POLL_INTERVAL_MS);

  return expectClipboard(expected, runCommand, deadline);
};

/**
 * The harness side of the clipboard (#60), as a second Wayland client: wl-clipboard's `wl-paste` and `wl-copy`,
 * which take keyboard focus with a surface of their own because cage offers no data-control protocol, and hand it
 * back when they exit. `clipboard-expect <text>` passes once the seat's selection reads `<text>`.
 * `clipboard-set <text>` makes `<text>` the selection and returns once it is set; `wl-copy` stays behind serving
 * it, as a source must, because the selection empties when its source exits, and it ends with the compositor. The
 * window reads it when its keyboard focus comes back, so a paste follows after a short sleep. Neither waits for
 * injected input still in flight, so a `clipboard-set` that must follow a key goes after an `await` or a
 * `clipboard-expect`.
 */
const runClipboardStep = async (
  step: string,
  runCommand: RunCommand,
  timeoutMilliseconds = CLIPBOARD_TIMEOUT_MS,
): Promise<string | null> => {
  if (step.startsWith(CLIPBOARD_EXPECT_PREFIX)) {
    return expectClipboard(step.slice(CLIPBOARD_EXPECT_PREFIX.length), runCommand, Date.now() + timeoutMilliseconds);
  }

  const text = step.slice(CLIPBOARD_SET_PREFIX.length);
  // The daemon wl-copy leaves serving the selection must not hold this command's output pipe open.
  const copied = await runCommand("sh", ["-c", 'exec wl-copy "$0" >/dev/null 2>&1', text]);

  return copied.succeeded ? null : `wl-copy could not make "${text}" the selection`;
};

export { createCommandRunner, isClipboardStep, runClipboardStep };
export type { RunCommand };
