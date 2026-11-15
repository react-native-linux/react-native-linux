const AWAIT_STEP_PREFIX = "await ";
const SCRIPT_START_INDEX = 0;
const AFTER_AWAIT_STEP_OFFSET = 1;

interface AwaitingInjection {
  readonly send: (stretch: readonly string[]) => void;
  readonly steps: readonly string[];
  /** The trace so far, and a bounded wait for a condition on it. */
  readonly trace: {
    readonly read: () => string;
    readonly waitFor: (isSatisfied: (trace: string) => boolean) => Promise<boolean>;
  };
}

/**
 * Sends `steps` to one running `rnl_inject` a stretch at a time, holding back what follows each `await <text>`
 * step (#719) until the trace shows `<text>` after the point where it ended when the stretch before began. The
 * wait is bounded, and the step itself never reaches the injector. Input that depends on the app's state, such as
 * a click on a control that has to finish moving first, goes out once the app reports that state rather than after
 * a sleep that guesses how long it takes under load. One injector for the whole script matters: a new one is a new
 * virtual pointer the window has to bind before its first event can land.
 */
const sendAwaitingTrace = async ({ send, steps, trace }: AwaitingInjection): Promise<string | null> => {
  const stretchStart = trace.read().length;
  const awaitStep = steps.find((step) => step.startsWith(AWAIT_STEP_PREFIX)) ?? null;

  if (awaitStep === null) {
    send(steps);

    return null;
  }

  const awaitIndex = steps.indexOf(awaitStep);
  const awaited = awaitStep.slice(AWAIT_STEP_PREFIX.length);

  send(steps.slice(SCRIPT_START_INDEX, awaitIndex));

  return (await trace.waitFor((text) => text.includes(awaited, stretchStart)))
    ? sendAwaitingTrace({ send, steps: steps.slice(awaitIndex + AFTER_AWAIT_STEP_OFFSET), trace })
    : `the trace never produced "${awaited}" before the steps after "${awaitStep}"`;
};

export { sendAwaitingTrace };
