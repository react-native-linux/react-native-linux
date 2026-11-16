import { createCommandRunner, isClipboardStep, runClipboardStep } from "./clipboard.ts";
import { describe, expect, it } from "vitest";
import type { RunCommand } from "./clipboard.ts";
import { env } from "node:process";

const EXPIRED_TIMEOUT_MS = -1;

/** A clipboard whose `wl-paste` answers come from `pastes` in order, and that records every command it ran. */
const createClipboard = (pastes: readonly string[], copySucceeds = true): { calls: string[]; run: RunCommand } => {
  const calls: string[] = [];

  return {
    calls,
    run: (command, commandArguments) => {
      const output = pastes[calls.length] ?? "";

      calls.push([command, ...commandArguments].join(" "));

      return Promise.resolve({ output, succeeded: command === "sh" ? copySucceeds : output !== "" });
    },
  };
};

describe("isClipboardStep", () => {
  it("names the two clipboard verbs and nothing the injector takes", () => {
    expect(isClipboardStep("clipboard-expect text")).toBe(true);
    expect(isClipboardStep("clipboard-set text")).toBe(true);
    expect(isClipboardStep("type clipboard-set")).toBe(false);
  });
});

describe("runClipboardStep", () => {
  it("retries wl-paste until the window's copy reaches the selection", async () => {
    const clipboard = createClipboard(["", "older copy", "Serial Ledger"]);

    expect(await runClipboardStep("clipboard-expect Serial Ledger", clipboard.run)).toBeNull();
    expect(clipboard.calls).toEqual(["wl-paste --no-newline", "wl-paste --no-newline", "wl-paste --no-newline"]);
  });

  it("reports what the selection held once the wait runs out", async () => {
    const clipboard = createClipboard(["older copy"]);

    expect(await runClipboardStep("clipboard-expect Serial Ledger", clipboard.run, EXPIRED_TIMEOUT_MS)).toBe(
      `the compositor's selection held "older copy", not "Serial Ledger"`,
    );
  });

  it("sets a selection that wl-copy stays behind to serve", async () => {
    const clipboard = createClipboard([]);

    expect(await runClipboardStep("clipboard-set from elsewhere", clipboard.run)).toBeNull();
    expect(clipboard.calls).toEqual(['sh -c exec wl-copy "$0" >/dev/null 2>&1 from elsewhere']);
  });

  it("fails when wl-copy could not set the selection", async () => {
    expect(await runClipboardStep("clipboard-set from elsewhere", createClipboard([], false).run)).toBe(
      'wl-copy could not make "from elsewhere" the selection',
    );
  });
});

describe("createCommandRunner", () => {
  it("returns a command's standard output and whether it succeeded", async () => {
    const run = createCommandRunner(env);

    expect(await run("sh", ["-c", "printf copied"])).toEqual({ output: "copied", succeeded: true });
    expect(await run("sh", ["-c", "exit 1"])).toEqual({ output: "", succeeded: false });
  });
});
