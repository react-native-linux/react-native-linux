/*
 * Launching and driving the window under test: its command line inside the compositor, and the `rnl_inject` run that
 * feeds it the scenario's steps. Grading what the run left behind is grade.ts.
 */
import type { Scenario } from "./scenario.ts";
import type { TraceWatch } from "./trace-await.ts";
import { createCommandRunner } from "./clipboard.ts";
import { runKeyboardAwareInjection } from "./keyboard-focus.ts";
import { scalePointerSteps } from "./pointer-scale.ts";
import { sendAwaitingTrace } from "./trace-await.ts";

/** Omission becomes `--no-decorations`, since cage and weston have no decoration manager; `[]` or a list stands. */
const resolveWindowFlags = (windowFlags?: readonly string[]): readonly string[] => windowFlags ?? ["--no-decorations"];

/**
 * Headless wlroots starts its output at 60 Hz and cage takes no mode flag, so a scenario naming `outputRefreshHz`
 * starts the window behind `wlr-randr`, which sets a 1280x720 custom mode over wlr-output-management first. `--off`
 * goes first because noble's cage aborts when a configuration re-enables an output still in its layout; see *At
 * 120 Hz* in docs/cpp-toolchain.md.
 */
const resolveCompositorChild = (outputRefreshHz: number | null, windowCommand: readonly string[]): readonly string[] =>
  outputRefreshHz === null
    ? windowCommand
    : [
        "sh",
        "-c",
        `wlr-randr --output HEADLESS-1 --off && wlr-randr --output HEADLESS-1 --on --custom-mode 1280x720@${String(outputRefreshHz)}Hz && exec "$0" "$@"`,
        ...windowCommand,
      ];

/** `rnl_inject`'s own exit status for a clean run cut short by the socket disappearing under it. */
const EXPECTED_CLOSE_STATUS = 1;

interface InjectionOutcome {
  readonly failure: string | null;
  readonly status: number | null;
}

/**
 * `expectsExitAfter` scenarios (window-decorations-close, the two protocol-error scenarios) drive `rnl_window`
 * into dying on its own mid-run — a closed window, a rejected protocol request — so `rnl_inject` loses its socket
 * and exits with status 1. Only that exact status is eligible: 2 is `rnl_inject`'s own usage/argument error and
 * `null` is a signal (the #233 `INJECT_TIMEOUT_MS` kill, among others) — neither is the socket disappearing, so
 * both keep failing the scenario regardless of the trace. `waitForExpectedClose` — which checks the trace for
 * `expectsExitAfter`'s substring — is asked only once status agrees, and accepting the failure still needs it to
 * resolve `true`, so a status-1 exit for an unrelated reason still fails.
 */
const resolveInjectionFailure = async (
  outcome: InjectionOutcome,
  expectsExitAfter: string | null,
  waitForExpectedClose: () => Promise<boolean>,
): Promise<string | null> => {
  if (outcome.failure === null || expectsExitAfter === null || outcome.status !== EXPECTED_CLOSE_STATUS) {
    return outcome.failure;
  }

  return (await waitForExpectedClose()) ? null : outcome.failure;
};

/**
 * A running `rnl_inject`: `send` queues script lines it executes as they arrive, `finish` ends the script.
 * `environment` is its connection to the compositor, which the clipboard steps of clipboard.ts share.
 */
interface Injector {
  readonly environment: Record<string, string | undefined>;
  readonly finish: () => Promise<InjectionOutcome>;
  readonly send: (steps: readonly string[]) => void;
}

interface InjectionRun {
  readonly scenario: Scenario;
  readonly startInjector: () => Injector;
  readonly trace: TraceWatch;
  readonly waitForKeyboardFocus: () => Promise<boolean>;
  readonly waitForExpectedClose: () => Promise<boolean>;
}

/**
 * `WindowMain.cpp`'s `announcePointerOnce` prints this once the window has bound `wl_pointer` and flushed the
 * request (#752). The seat advertises a pointer only once `rnl_inject` creates its virtual one, and a button the
 * compositor handles before that bind reaches no one, so a click with no step before it was lost.
 */
const POINTER_BOUND_TRACE_LINE = "[rnl-seat] pointer bound";

/**
 * Runs `run.scenario.steps` through one `rnl_inject`, threading its exit status to `resolveInjectionFailure` above.
 * `scalePointerSteps` converts every `move`/`click` line's coordinates from the logical pixels a scenario is
 * authored in to `rnl_inject`'s own surface pixels first, so the same rounding rule #374 gave the surface side
 * carries the injected coordinate rather than the raw logical one. The first step waits for the window to bind the
 * injector's pointer, and the keyboard split only holds the rest back: a second injector would be a second virtual
 * pointer, bound again.
 */
const injectAndResolveFailure = async (run: InjectionRun): Promise<string | null> => {
  const injector = run.startInjector();
  const runCommand = createCommandRunner(injector.environment);
  const failure = await runKeyboardAwareInjection(
    [`await ${POINTER_BOUND_TRACE_LINE}`, ...scalePointerSteps(run.scenario.steps)],
    (steps) => sendAwaitingTrace({ runCommand, send: injector.send, steps, trace: run.trace }),
    run.waitForKeyboardFocus,
  );
  const outcome = await injector.finish();

  return resolveInjectionFailure(
    { failure: failure ?? outcome.failure, status: outcome.status },
    run.scenario.expectsExitAfter,
    run.waitForExpectedClose,
  );
};

export { injectAndResolveFailure, resolveCompositorChild, resolveInjectionFailure, resolveWindowFlags };
export type { Injector };
