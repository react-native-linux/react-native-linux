import {
  KEYBOARD_FOCUS_TRACE_LINE,
  hasKeyboardSteps,
  isKeyboardFocused,
  runKeyboardAwareInjection,
  splitStepsAtKeyboardFocus,
} from "./keyboard-focus.ts";
import { describe, expect, it, vi } from "vitest";

const ONE_CALL = 1;
const FIRST_CALL = 1;
const SECOND_CALL = 2;

describe("hasKeyboardSteps", () => {
  it("reports true for a key step", () => {
    expect(hasKeyboardSteps(["sleep 500", "key Tab press"])).toBe(true);
  });

  it("reports true for a type step", () => {
    expect(hasKeyboardSteps(["sleep 500", "type Hi"])).toBe(true);
  });

  it("reports false for a scenario with only pointer and sleep steps", () => {
    expect(hasKeyboardSteps(["sleep 500", "move 100 120", "click 100 120"])).toBe(false);
  });

  it('does not match a step that merely contains "key" or "type" mid-word', () => {
    expect(hasKeyboardSteps(["monkey 1 2"])).toBe(false);
  });
});

describe("splitStepsAtKeyboardFocus", () => {
  it("keeps everything in beforeFocus when there is no keyboard step", () => {
    expect(splitStepsAtKeyboardFocus(["sleep 500", "move 100 120", "click 100 120"])).toEqual({
      afterFocus: [],
      beforeFocus: ["sleep 500", "move 100 120", "click 100 120"],
    });
  });

  it("splits at the first key step, keeping it and everything after in afterFocus", () => {
    expect(
      splitStepsAtKeyboardFocus(["sleep 500", "key Tab press", "key Tab release", "sleep 200", "type Hi"]),
    ).toEqual({
      afterFocus: ["key Tab press", "key Tab release", "sleep 200", "type Hi"],
      beforeFocus: ["sleep 500"],
    });
  });

  it("splits at the first type step when it comes before any key step", () => {
    expect(splitStepsAtKeyboardFocus(["sleep 500", "type Hi", "key Return press"])).toEqual({
      afterFocus: ["type Hi", "key Return press"],
      beforeFocus: ["sleep 500"],
    });
  });

  it("puts a leading keyboard step straight into afterFocus with an empty beforeFocus", () => {
    expect(splitStepsAtKeyboardFocus(["key Tab press"])).toEqual({ afterFocus: ["key Tab press"], beforeFocus: [] });
  });
});

describe("isKeyboardFocused", () => {
  it("reports false when the trace has not printed the line yet", () => {
    expect(isKeyboardFocused("shadow-input: committed surface 1\n")).toBe(false);
  });

  it("reports true once the trace includes the line", () => {
    expect(isKeyboardFocused(`shadow-input: committed surface 1\n${KEYBOARD_FOCUS_TRACE_LINE}\n`)).toBe(true);
  });
});

describe("runKeyboardAwareInjection without a keyboard step", () => {
  it("injects everything in one call and never waits", async () => {
    const inject = vi.fn(() => null);
    const waitForKeyboardFocus = vi.fn(() => Promise.resolve(true));

    const failure = await runKeyboardAwareInjection(["sleep 500", "click 1 1"], inject, waitForKeyboardFocus);

    expect(failure).toBeNull();
    expect(inject).toHaveBeenCalledTimes(ONE_CALL);
    expect(inject).toHaveBeenCalledWith(["sleep 500", "click 1 1"]);
    expect(waitForKeyboardFocus).not.toHaveBeenCalled();
  });
});

describe("runKeyboardAwareInjection with a keyboard step", () => {
  it("reports the beforeFocus failure and never injects afterFocus or waits", async () => {
    const inject = vi.fn(() => "rnl_inject exited with status 1");
    const waitForKeyboardFocus = vi.fn(() => Promise.resolve(true));

    const failure = await runKeyboardAwareInjection(["sleep 500", "key Tab press"], inject, waitForKeyboardFocus);

    expect(failure).toBe("rnl_inject exited with status 1");
    expect(inject).toHaveBeenCalledTimes(ONE_CALL);
    expect(waitForKeyboardFocus).not.toHaveBeenCalled();
  });

  it("waits for keyboard focus before injecting afterFocus", async () => {
    const inject = vi.fn(() => null);
    const waitForKeyboardFocus = vi.fn(() => Promise.resolve(true));

    const failure = await runKeyboardAwareInjection(
      ["sleep 500", "key Tab press", "type Hi"],
      inject,
      waitForKeyboardFocus,
    );

    expect(failure).toBeNull();
    expect(waitForKeyboardFocus).toHaveBeenCalledTimes(ONE_CALL);
    expect(inject).toHaveBeenNthCalledWith(FIRST_CALL, ["sleep 500"]);
    expect(inject).toHaveBeenNthCalledWith(SECOND_CALL, ["key Tab press", "type Hi"]);
  });

  it("fails with a clear reason when keyboard focus never arrives, and never injects afterFocus", async () => {
    const inject = vi.fn(() => null);
    const waitForKeyboardFocus = vi.fn(() => Promise.resolve(false));

    const failure = await runKeyboardAwareInjection(["key Tab press"], inject, waitForKeyboardFocus);

    expect(failure).toBe(`the window never printed "${KEYBOARD_FOCUS_TRACE_LINE}" before the first keyboard step`);
    expect(inject).toHaveBeenCalledTimes(ONE_CALL);
    expect(inject).toHaveBeenCalledWith([]);
  });
});
