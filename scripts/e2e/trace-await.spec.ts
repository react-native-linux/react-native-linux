import { describe, expect, it } from "vitest";
import { sendAwaitingTrace } from "./trace-await.ts";

type TraceWatch = Parameters<typeof sendAwaitingTrace>[number]["trace"];

interface Rig {
  readonly sent: (readonly string[])[];
  readonly send: (stretch: readonly string[]) => void;
  readonly trace: TraceWatch;
}

/** A trace that grows by `printedPerStretch[n]` when stretch `n` is sent, and a wait that checks it once. */
const createRig = (printedPerStretch: readonly string[], initialTrace = ""): Rig => {
  const sent: (readonly string[])[] = [];
  let text = initialTrace;

  return {
    send: (stretch) => {
      text += printedPerStretch[sent.length] ?? "";
      sent.push(stretch);
    },
    sent,
    trace: { read: () => text, waitFor: (isSatisfied) => Promise.resolve(isSatisfied(text)) },
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
  });
});
