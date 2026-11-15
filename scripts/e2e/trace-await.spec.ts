import { describe, expect, it } from "vitest";
import type { TraceWatch } from "./trace-await.ts";
import { sendAwaitingTrace } from "./trace-await.ts";

interface Rig {
  readonly sent: (readonly string[])[];
  readonly send: (stretch: readonly string[]) => void;
  readonly trace: TraceWatch;
  readonly waitOutcomes: boolean[];
}

/**
 * A trace that grows by `printedPerStretch[n]` when stretch `n` is sent, and a wait that checks it once and records
 * whether its condition held, so a test can tell a wait that ended early from one that ran out.
 */
const createRig = (printedPerStretch: readonly string[], initialTrace = "", isClosed = false): Rig => {
  const sent: (readonly string[])[] = [];
  const waitOutcomes: boolean[] = [];
  let text = initialTrace;

  return {
    send: (stretch) => {
      text += printedPerStretch[sent.length] ?? "";
      sent.push(stretch);
    },
    sent,
    trace: {
      isClosed: () => isClosed,
      read: () => text,
      waitFor: (isSatisfied) => {
        const outcome = isSatisfied(text);

        waitOutcomes.push(outcome);

        return Promise.resolve(outcome);
      },
    },
    waitOutcomes,
  };
};

describe("sendAwaitingTrace", () => {
  it("sends a script without await steps in one stretch", async () => {
    const rig = createRig([]);

    expect(await sendAwaitingTrace({ send: rig.send, steps: ["move 1 2", "click 1 2"], trace: rig.trace })).toBeNull();
    expect(rig.sent).toEqual([["move 1 2", "click 1 2"]]);
  });

  it("holds back what follows each await step until the awaited text traces", async () => {
    const rig = createRig(["settled a\n", "settled b\n"]);
    const steps = ["pan 1 0", "await settled a", "click 1 2", "pan 2 0", "await settled b", "click 3 4"];

    expect(await sendAwaitingTrace({ send: rig.send, steps, trace: rig.trace })).toBeNull();
    expect(rig.sent).toEqual([["pan 1 0"], ["click 1 2", "pan 2 0"], ["click 3 4"]]);
  });

  it("does not count text that traced before the stretch began, and sends nothing after the await", async () => {
    const rig = createRig([], "settled\n");

    expect(
      await sendAwaitingTrace({ send: rig.send, steps: ["pan 1 0", "await settled", "click 1 2"], trace: rig.trace }),
    ).toBe('the trace never produced "settled" before the steps after "await settled"');
    expect(rig.sent).toEqual([["pan 1 0"]]);
    expect(rig.waitOutcomes).toEqual([false]);
  });

  it("ends the wait as soon as the window closes its trace, and says the window closed", async () => {
    const rig = createRig([], "", true);

    expect(
      await sendAwaitingTrace({ send: rig.send, steps: ["pan 1 0", "await settled", "click 1 2"], trace: rig.trace }),
    ).toBe('the window closed before the trace produced "settled" for "await settled"');
    expect(rig.sent).toEqual([["pan 1 0"]]);
    expect(rig.waitOutcomes).toEqual([true]);
  });
});
