import { findAutomationSocketPath, findSurfaceChildren, requestAutomation } from "./automation.ts";
import { setTimeout as delay } from "node:timers/promises";

const FATAL_ERROR_PREFIX = "[js-error] fatal";
const CONSOLE_ERROR_PREFIX = "[console.error] ";
const RUNNING_MAIN_TRACE_LINE = 'Running "main"';
const AUTOMATION_TIMEOUT_MS = 10_000;
const EMPTY_LENGTH = 0;
const MILLISECONDS_PER_SECOND = 1000;
const SECONDS_FRACTION_DIGITS = 1;
/**
 * The fixture's window runs the `dev` preset, whose Debug Hermes evaluates T3 Code's development bundle in about 100 s
 * on an idle machine before `"main"` runs; six times that leaves room for a loaded runner.
 */
const STARTUP_TIMING = { pollIntervalMs: 1000, timeoutMs: 600_000 };

/** The trace of a running window so far, and whether the window has closed it. */
interface StartupWatch {
  readonly isClosed: () => boolean;
  readonly read: () => string;
}

/** Milliseconds from launch to `"main"` running, which is where the bundle's evaluation ended, and to the first commit. */
interface StartupTimings {
  readonly firstCommitMs: number | null;
  readonly runningMainMs: number | null;
}

/**
 * The errors startup reported, in trace order and each once (React Native prints a console line both through its
 * logging hook and the console it wraps): a fatal JavaScript error, or any `console.error`, because React reports an
 * uncaught render error, such as a component throwing on a missing TurboModule, only there and then shows nothing.
 */
const findStartupErrors = (trace: string): readonly string[] => [
  ...new Set(
    trace.split("\n").filter((line) => line.startsWith(FATAL_ERROR_PREFIX) || line.startsWith(CONSOLE_ERROR_PREFIX)),
  ),
];

/** Whether the window's visual tree holds a view under the surface root: React committed T3 Code's first screen. */
const hasCommittedView = async (trace: string): Promise<boolean> => {
  const socketPath = findAutomationSocketPath(trace);

  if (socketPath === null) {
    return false;
  }

  const { result } = await requestAutomation(socketPath, { command: "DumpVisualTree" }, AUTOMATION_TIMEOUT_MS);
  const children = result === null ? null : findSurfaceChildren(result);

  return Array.isArray(children) && children.length > EMPTY_LENGTH;
};

/**
 * Polls the visual tree every `pollIntervalMs` until T3 Code commits a view, for at most `timeoutMs` from
 * `launchedAt`. It stops early once the window closes or startup reports an error, since an uncaught render error
 * leaves the root empty for good.
 */
const waitForFirstCommit = async (
  watch: StartupWatch,
  launchedAt: number,
  { pollIntervalMs, timeoutMs } = STARTUP_TIMING,
): Promise<StartupTimings> => {
  let runningMainMs: number | null = null;

  while (
    Date.now() - launchedAt < timeoutMs &&
    !watch.isClosed() &&
    findStartupErrors(watch.read()).length === EMPTY_LENGTH
  ) {
    runningMainMs ??= watch.read().includes(RUNNING_MAIN_TRACE_LINE) ? Date.now() - launchedAt : null;

    if (await hasCommittedView(watch.read())) {
      return { firstCommitMs: Date.now() - launchedAt, runningMainMs };
    }

    await delay(pollIntervalMs);
  }

  return { firstCommitMs: null, runningMainMs };
};

const formatSeconds = (milliseconds: number | null): string =>
  milliseconds === null ? "not seen" : `${(milliseconds / MILLISECONDS_PER_SECOND).toFixed(SECONDS_FRACTION_DIGITS)} s`;

const describeStartupTimings = ({ firstCommitMs, runningMainMs }: StartupTimings): string =>
  `time to "main": ${formatSeconds(runningMainMs)}, time to the first commit: ${formatSeconds(firstCommitMs)}`;

export { describeStartupTimings, findStartupErrors, formatSeconds, waitForFirstCommit };
export type { StartupWatch };
