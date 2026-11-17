import { findAutomationSocketPath, requestAutomation } from "./automation.ts";
import { setTimeout as delay } from "node:timers/promises";
import { isRecord } from "./fields.ts";

const FATAL_ERROR_PREFIX = "[js-error] fatal";
const CONSOLE_ERROR_PREFIX = "[console.error] ";
const RUNNING_MAIN_TRACE_LINE = 'Running "main"';
const MAIN_SCREEN_BUTTON_NAME = "New thread";
const AUTOMATION_TIMEOUT_MS = 10_000;
const EMPTY_LENGTH = 0;
const MILLISECONDS_PER_SECOND = 1000;
const SECONDS_FRACTION_DIGITS = 1;
/**
 * The fixture's window runs the `dev` preset, which evaluates T3 Code's development bundle in about 3 s on an idle
 * machine before `"main"` runs, and the main screen follows within about 2 s (#814). The timeout leaves room for a
 * loaded runner and is still short of the 90 s a Hermes with slow-debug checks takes to reach `"main"`. The window
 * stays open `settleMs` past the main screen for what renders next to report.
 */
const STARTUP_TIMING: StartupTiming = { pollIntervalMs: 1000, settleMs: 5000, timeoutMs: 60_000 };

/**
 * How long the wait polls, settles and waits in all, and, when a spec passes a virtual clock so the settle is not a
 * race against wall-clock time, where it reads the time and how it sleeps.
 */
interface StartupTiming {
  readonly now?: () => number;
  readonly pollIntervalMs: number;
  readonly settleMs: number;
  readonly sleep?: (milliseconds: number) => Promise<unknown>;
  readonly timeoutMs: number;
}

/** The trace of a running window so far, and whether the window has closed it. */
interface StartupWatch {
  readonly isClosed: () => boolean;
  readonly read: () => string;
}

/**
 * Milliseconds from launch to `"main"` running, which is where the bundle's evaluation ended, and to the main screen,
 * with the names the last accessibility tree the wait read held, which is what a run that never reached it saw.
 */
interface StartupOutcome {
  readonly lastNames: readonly string[];
  readonly mainScreenMs: number | null;
  readonly runningMainMs: number | null;
}

type AccessibilityNode = Record<string, unknown>;

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

/** Every node of an accessibility projection, each before its children. */
const flattenAccessibilityNodes = (nodes: unknown): readonly AccessibilityNode[] =>
  Array.isArray(nodes)
    ? nodes
        .filter((node): node is AccessibilityNode => isRecord(node))
        .flatMap((node) => [node, ...flattenAccessibilityNodes(node["children"])])
    : [];

/**
 * T3 Code's "New thread" button, which only its main screen's empty workspace shows, LogBox never does, and which
 * names itself through the text inside it.
 */
const isMainScreenButton = (node: AccessibilityNode): boolean =>
  node["role"] === "button" &&
  [node, ...flattenAccessibilityNodes(node["children"])].some(({ name }) => name === MAIN_SCREEN_BUTTON_NAME);

/** The window's accessibility tree, flattened; empty while the window has no automation channel or refuses. */
const readAccessibilityNodes = async (trace: string): Promise<readonly AccessibilityNode[]> => {
  const socketPath = findAutomationSocketPath(trace);

  if (socketPath === null) {
    return [];
  }

  const { result } = await requestAutomation(socketPath, { command: "DumpAccessibilityTree" }, AUTOMATION_TIMEOUT_MS);

  return flattenAccessibilityNodes(result?.["nodes"]);
};

/** Milliseconds since `launchedAt` once `trace` shows `"main"` running, or null before then. */
const findRunningMainMs = (trace: string, elapsedMs: number): number | null =>
  trace.includes(RUNNING_MAIN_TRACE_LINE) ? elapsedMs : null;

/**
 * Polls the accessibility tree every `pollIntervalMs` until it holds T3 Code's main screen, for at most `timeoutMs`
 * from `launchedAt`, then holds the window `settleMs` longer. A main screen whose answer arrives after the deadline is
 * a timeout. It stops early once the window closes or startup reports an error, since an uncaught render error leaves
 * the root empty for good. `"main"` is sampled after each answer, before the settle, and once more on the way out.
 */
const waitForMainScreen = async (
  watch: StartupWatch,
  launchedAt: number,
  { now = Date.now, pollIntervalMs, settleMs, sleep = delay, timeoutMs } = STARTUP_TIMING,
): Promise<StartupOutcome> => {
  let runningMainMs: number | null = null;
  let mainScreenMs: number | null = null;
  let nodes: readonly AccessibilityNode[] = [];

  while (
    mainScreenMs === null &&
    now() - launchedAt < timeoutMs &&
    !watch.isClosed() &&
    findStartupErrors(watch.read()).length === EMPTY_LENGTH
  ) {
    nodes = await readAccessibilityNodes(watch.read());
    runningMainMs ??= findRunningMainMs(watch.read(), now() - launchedAt);
    mainScreenMs =
      nodes.some((node) => isMainScreenButton(node)) && now() - launchedAt < timeoutMs ? now() - launchedAt : null;
    await sleep(mainScreenMs === null ? pollIntervalMs : settleMs);
  }

  return {
    lastNames: nodes.flatMap(({ name }) => (typeof name === "string" ? [name] : [])),
    mainScreenMs,
    runningMainMs: runningMainMs ?? findRunningMainMs(watch.read(), now() - launchedAt),
  };
};

const formatSeconds = (milliseconds: number | null): string =>
  milliseconds === null ? "not seen" : `${(milliseconds / MILLISECONDS_PER_SECOND).toFixed(SECONDS_FRACTION_DIGITS)} s`;

const describeStartupTimings = ({ mainScreenMs, runningMainMs }: StartupOutcome): string =>
  `time to "main": ${formatSeconds(runningMainMs)}, time to the main screen: ${formatSeconds(mainScreenMs)}`;

export { MAIN_SCREEN_BUTTON_NAME, describeStartupTimings, findStartupErrors, formatSeconds, waitForMainScreen };
export type { StartupOutcome, StartupTiming, StartupWatch };
