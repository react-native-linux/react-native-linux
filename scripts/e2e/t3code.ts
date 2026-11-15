import { FIRST_PRESENTED_FRAME_TRACE_LINE, hasPresentedFirstFrame } from "./window-readiness.ts";

/** The T3 Code revision docs/research/t3code-linux.md surveyed, and the one the fixture runs. */
const T3CODE_REVISION = "238678951ac4945969b8203ae2996b5891b6ffce";

const FATAL_ERROR_PREFIX = "[js-error] fatal";
const PAIRING_URL_PATTERN = /^Pairing URL: (?<pairingUrl>\S+)$/mu;
const SUCCESSFUL_EXIT_STATUS = 0;
const TAIL_LINE_COUNT = 40;

/** What the fixture saw of one window run: the wayland socket, the trace, and how the compositor ended. */
interface WindowRun {
  readonly exitCode: number | null;
  readonly hasSocket: boolean;
  readonly isTraceClosed: boolean;
  readonly signal: string | null;
  readonly trace: string;
}

/** The pairing URL `t3 serve` prints once it is ready, carrying its port and one-time startup token; null until then. */
const readPairingUrl = (serverOutput: string): string | null =>
  PAIRING_URL_PATTERN.exec(serverOutput)?.groups?.["pairingUrl"] ?? null;

/** The link that opens T3 Code's pairing form and connects at once: `features/connection/devAutoConnect.ts`. */
const buildPairingLink = (pairingUrl: string): string =>
  `t3code://connections/new?${new URLSearchParams([
    ["pairingUrl", pairingUrl],
    ["autoConnect", "1"],
  ]).toString()}`;

/** The last lines of a log, which is what a failure prints of it. */
const tailLines = (text: string): string => text.trimEnd().split("\n").slice(-TAIL_LINE_COUNT).join("\n");

/**
 * Why a run is not a pass, the first fatal JavaScript error leading as where startup stopped; empty only when the
 * socket came up, the window presented its first frame, and the compositor exited successfully within the run.
 */
const gradeWindowRun = (run: WindowRun): readonly string[] => {
  const [firstFatalError = null] = run.trace.split("\n").filter((line) => line.startsWith(FATAL_ERROR_PREFIX));

  return [
    ...(firstFatalError === null ? [] : [`startup stopped at\n${firstFatalError}`]),
    ...(run.hasSocket ? [] : ["the compositor never created a wayland socket"]),
    ...(hasPresentedFirstFrame(run.trace) ? [] : [`the window never printed "${FIRST_PRESENTED_FRAME_TRACE_LINE}"`]),
    ...(run.isTraceClosed ? [] : ["the window did not exit within the run timeout"]),
    ...(run.exitCode === SUCCESSFUL_EXIT_STATUS
      ? []
      : [`the compositor ended with exit code ${String(run.exitCode)}, signal ${String(run.signal)}`]),
  ];
};

export { T3CODE_REVISION, buildPairingLink, gradeWindowRun, readPairingUrl, tailLines };
