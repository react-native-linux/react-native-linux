import { describe, expect, it } from "vitest";
import type { RunCommand } from "./clipboard.ts";
import type { TraceWatch } from "./trace-await.ts";
import { sendAwaitingTrace } from "./trace-await.ts";

interface Rig {
  readonly runCommand: RunCommand;
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
    runCommand: (command, commandArguments) => {
      sent.push([command, ...commandArguments]);

      return Promise.resolve({ output: "a", succeeded: command === "wl-paste" });
    },
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

const sendThrough = (rig: Rig, steps: readonly string[]): Promise<string | null> =>
  sendAwaitingTrace({ runCommand: rig.runCommand, send: rig.send, steps, trace: rig.trace });

describe("sendAwaitingTrace", () => {
  it("sends a script without await steps in one stretch", async () => {
    const rig = createRig([]);

    expect(await sendThrough(rig, ["move 1 2", "click 1 2"])).toBeNull();
    expect(rig.sent).toEqual([["move 1 2", "click 1 2"]]);
  });

  it("holds back what follows each await step until the awaited text traces", async () => {
    const rig = createRig(["settled a\n", "settled b\n"]);
    const steps = ["pan 1 0", "await settled a", "click 1 2", "pan 2 0", "await settled b", "click 3 4"];

    expect(await sendThrough(rig, steps)).toBeNull();
    expect(rig.sent).toEqual([["pan 1 0"], ["click 1 2", "pan 2 0"], ["click 3 4"]]);
  });

  it("does not count text that traced before the stretch began, and sends nothing after the await", async () => {
    const rig = createRig([], "settled\n");

    expect(await sendThrough(rig, ["pan 1 0", "await settled", "click 1 2"])).toBe(
      'the trace never produced "settled" before the steps after "await settled"',
    );
    expect(rig.sent).toEqual([["pan 1 0"]]);
    expect(rig.waitOutcomes).toEqual([false]);
  });

  it("ends the wait as soon as the window closes its trace, and says the window closed", async () => {
    const rig = createRig([], "", true);

    expect(await sendThrough(rig, ["pan 1 0", "await settled", "click 1 2"])).toBe(
      'the window closed before the trace produced "settled" for "await settled"',
    );
    expect(rig.sent).toEqual([["pan 1 0"]]);
    expect(rig.waitOutcomes).toEqual([true]);
  });
});

describe("sendAwaitingTrace with clipboard steps", () => {
  it("runs each clipboard step between the stretches around it, and stops at the first that fails", async () => {
    const rig = createRig([]);
    const steps = ["key a", "clipboard-expect a", "key b", "clipboard-set unread", "key c"];

    expect(await sendThrough(rig, steps)).toBe('wl-copy could not make "unread" the selection');
    expect(rig.sent).toEqual([
      ["key a"],
      ["wl-paste", "--no-newline"],
      ["key b"],
      ["sh", "-c", 'exec wl-copy "$0" >/dev/null 2>&1', "unread"],
    ]);
  });
});
