import { isClipboardStep, runClipboardStep } from "./clipboard.ts";
import type { RunCommand } from "./clipboard.ts";

const AWAIT_STEP_PREFIX = "await ";
const SCRIPT_START_INDEX = 0;
const AFTER_AWAIT_STEP_OFFSET = 1;

/** The trace so far, whether the window has closed it, and a bounded wait for a condition on it. */
interface TraceWatch {
  readonly isClosed: () => boolean;
  readonly read: () => string;
  readonly waitFor: (isSatisfied: (trace: string) => boolean) => Promise<boolean>;
}

interface AwaitingInjection {
  readonly runCommand: RunCommand;
  readonly send: (stretch: readonly string[]) => void;
  readonly steps: readonly string[];
  readonly trace: TraceWatch;
}

/**
 * Waits for the text an `await <text>` step names to trace after `stretchStart`, or for the window to close the
 * trace first, and says which went wrong if the text never came.
 */
const awaitTrace = async (trace: TraceWatch, awaitStep: string, stretchStart: number): Promise<string | null> => {
  const awaited = awaitStep.slice(AWAIT_STEP_PREFIX.length);
  const isAwaitedTraced = (text: string): boolean => text.includes(awaited, stretchStart);

  await trace.waitFor((text) => isAwaitedTraced(text) || trace.isClosed());

  if (isAwaitedTraced(trace.read())) {
    return null;
  }

  return trace.isClosed()
    ? `the window closed before the trace produced "${awaited}" for "${awaitStep}"`
    : `the trace never produced "${awaited}" before the steps after "${awaitStep}"`;
};

const isHeldStep = (step: string): boolean => step.startsWith(AWAIT_STEP_PREFIX) || isClipboardStep(step);

/**
 * Sends `steps` to one running `rnl_inject` a stretch at a time, holding back what follows each `await <text>`
 * step (#719) until the trace shows `<text>` after the point where it ended when the stretch before began. The
 * wait is bounded and ends early when the window closes its trace, and the step itself never reaches the injector.
 * A clipboard step (#60) is held the same way and run through `runCommand` instead of awaited.
 * Input that depends on the app's state, such as a click on a control that has to finish moving first, goes out
 * once the app reports that state rather than after a sleep that guesses how long it takes under load. One
 * injector for the whole script matters: a new one is a new virtual pointer the window has to bind before its
 * first event can land.
 */
const sendAwaitingTrace = async ({ runCommand, send, steps, trace }: AwaitingInjection): Promise<string | null> => {
  const stretchStart = trace.read().length;
  const heldStep = steps.find((step) => isHeldStep(step)) ?? null;

  if (heldStep === null) {
    send(steps);

    return null;
  }

  const heldIndex = steps.indexOf(heldStep);

  send(steps.slice(SCRIPT_START_INDEX, heldIndex));

  const failure = isClipboardStep(heldStep)
    ? await runClipboardStep(heldStep, runCommand)
    : await awaitTrace(trace, heldStep, stretchStart);

  return (
    failure ?? sendAwaitingTrace({ runCommand, send, steps: steps.slice(heldIndex + AFTER_AWAIT_STEP_OFFSET), trace })
  );
};

export { sendAwaitingTrace };
export type { TraceWatch };
