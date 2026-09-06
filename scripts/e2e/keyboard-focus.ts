const SCRIPT_START_INDEX = 0;
const NOT_FOUND_INDEX = -1;
const EMPTY_LENGTH = 0;
const KEYBOARD_STEP_PATTERN = /^(?:key|type) /u;

/**
 * `WindowMain.cpp`'s `announceKeyboardFocusOnce` prints this unconditionally, independent of `--window-debug`,
 * the first time `wl_keyboard.enter` reaches the surface — see *Desktop lifecycle contract (#218)* in
 * docs/cpp-toolchain.md. `runKeyboardAwareInjection` below waits for it before the first keyboard step instead of
 * sleeping a fixed amount, which is the #304 fix. The tag is `[rnl-focus]`, not `[rnl-window]`: `ERROR_TRACE_
 * PATTERNS` in `scripts/e2e/scenario.ts` treats any `[rnl-window]` line as a fault, and this one is not.
 */
const KEYBOARD_FOCUS_TRACE_LINE = "[rnl-focus] keyboard entered";

/** Whether any `rnl_inject` step in `steps` presses a key or types text. */
const hasKeyboardSteps = (steps: readonly string[]): boolean => steps.some((step) => KEYBOARD_STEP_PATTERN.test(step));

interface StepsAroundKeyboardFocus {
  readonly afterFocus: readonly string[];
  readonly beforeFocus: readonly string[];
}

/**
 * Splits a scenario's steps at its first keyboard step, so the driver can run `beforeFocus` through `rnl_inject`,
 * wait for keyboard focus, and only then run `afterFocus` through a second invocation. A scenario with no
 * keyboard step gets everything back in `beforeFocus` and an empty `afterFocus`.
 */
const splitStepsAtKeyboardFocus = (steps: readonly string[]): StepsAroundKeyboardFocus => {
  const focusStepIndex = steps.findIndex((step) => KEYBOARD_STEP_PATTERN.test(step));

  if (focusStepIndex === NOT_FOUND_INDEX) {
    return { afterFocus: [], beforeFocus: steps };
  }

  return { afterFocus: steps.slice(focusStepIndex), beforeFocus: steps.slice(SCRIPT_START_INDEX, focusStepIndex) };
};

/** Whether `trace` already shows the compositor gave the surface keyboard focus. */
const isKeyboardFocused = (trace: string): boolean => trace.includes(KEYBOARD_FOCUS_TRACE_LINE);

/**
 * Runs `steps` through `inject`, split around the first keyboard step: `beforeFocus` first, then
 * `waitForKeyboardFocus` before `afterFocus`, instead of a fixed sleep racing the compositor's `wl_keyboard.enter`
 * — the #304 fix. A scenario with no keyboard step never calls `waitForKeyboardFocus` at all.
 */
const runKeyboardAwareInjection = async (
  steps: readonly string[],
  inject: (subset: readonly string[]) => string | null,
  waitForKeyboardFocus: () => Promise<boolean>,
): Promise<string | null> => {
  const { afterFocus, beforeFocus } = splitStepsAtKeyboardFocus(steps);
  const beforeFocusFailure = inject(beforeFocus);

  if (beforeFocusFailure !== null || afterFocus.length === EMPTY_LENGTH) {
    return beforeFocusFailure;
  }

  if (!(await waitForKeyboardFocus())) {
    return `the window never printed "${KEYBOARD_FOCUS_TRACE_LINE}" before the first keyboard step`;
  }

  return inject(afterFocus);
};

export {
  hasKeyboardSteps,
  isKeyboardFocused,
  KEYBOARD_FOCUS_TRACE_LINE,
  runKeyboardAwareInjection,
  splitStepsAtKeyboardFocus,
};
