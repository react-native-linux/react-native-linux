import type { StartupOutcome, StartupTiming, StartupWatch } from "./t3code-startup.ts";
import { afterEach, beforeEach, describe, expect, it } from "vitest";
import { describeStartupTimings, findStartupErrors, waitForMainScreen } from "./t3code-startup.ts";
import { mkdtempSync, rmSync } from "node:fs";
import { createServer } from "node:net";
import { once } from "node:events";
import path from "node:path";
import { setImmediate } from "node:timers/promises";
import { tmpdir } from "node:os";

const statusBarError =
  "[console.error] Invariant Violation: TurboModuleRegistry.getEnforcing(...): 'StatusBarManager' could not be found.";
const componentError = "[console.error] TypeError: Cannot read property 'default' of undefined";
const fatalError = "[js-error] fatal Error: Cannot find native module 'ExpoUpdates'";
const runningMain = 'Running "main" with {"rootTag":1}';
const SETTLE_MS = 50;
const timing = { pollIntervalMs: 1, settleMs: SETTLE_MS, timeoutMs: 5000 };
const shortTiming = { pollIntervalMs: 1, settleMs: SETTLE_MS, timeoutMs: 20 };
const virtualTiming = { pollIntervalMs: 1, settleMs: SETTLE_MS, timeoutMs: 5 };
const VIRTUAL_LAUNCH_MS = 1_000_000;
const NO_ELAPSED_MS = 0;
const POLLS_BEFORE_MAIN_SCREEN = 2;
/** The two polls answered with a refusal and LogBox's toast alone, each followed by one poll interval. */
const MAIN_SCREEN_AFTER_MS = POLLS_BEFORE_MAIN_SCREEN * timing.pollIntervalMs;

/** A promise and the call that resolves it. */
interface Latch {
  readonly open: () => void;
  readonly promise: Promise<unknown>;
}

const createLatch = (): Latch => {
  const target = new EventTarget();

  return { open: (): boolean => target.dispatchEvent(new Event("open")), promise: once(target, "open") };
};

/** A virtual clock, with the start and release of its held sleep. */
interface VirtualClock extends Required<Pick<StartupTiming, "now" | "sleep">> {
  readonly release: () => void;
  readonly requested: Promise<unknown>;
  readonly sleeps: readonly number[];
}

/**
 * A clock that stands still between sleeps and records each one, so time moves only when the wait sleeps. A sleep of
 * `heldMs` reports that it started through `requested` and lasts until `release` is called, so a spec can see what
 * the wait does while it is in progress.
 */
const createVirtualClock = (heldMs: number | null = null): VirtualClock => {
  let time = VIRTUAL_LAUNCH_MS;
  const sleeps: number[] = [];
  const [requested, held] = [createLatch(), createLatch()];

  return {
    now: (): number => time,
    release: held.open,
    requested: requested.promise,
    sleep: async (milliseconds: number): Promise<void> => {
      sleeps.push(milliseconds);
      if (milliseconds === heldMs) {
        requested.open();
        await held.promise;
      }
      time += milliseconds;
    },
    sleeps,
  };
};

describe("findStartupErrors", () => {
  it("answers each fatal error and console.error once, in trace order, and ignores logs and warnings", () => {
    const trace = [
      "Require cycle: projects.ts -> projectEntities.ts -> projects.ts",
      statusBarError,
      "'Failed to initialize devtools client', { _type: 'error' }",
      statusBarError,
      "[js-error] non-fatal Error: handled",
      fatalError,
      componentError,
    ].join("\n");

    expect(findStartupErrors(trace)).toStrictEqual([statusBarError, fatalError, componentError]);
  });
});

const treeLine = (nodes: readonly unknown[]): string => `${JSON.stringify({ ok: true, result: { nodes } })}\n`;
const logBoxToast = {
  children: [
    { name: "8", role: "text", tag: 14 },
    { name: "Failed to initialize devtools client", role: "text", tag: 24 },
  ],
  role: "none",
  tag: 36,
};
const newThreadButton = { children: [{ name: "New thread", role: "text", tag: 126 }], role: "button", tag: 128 };
const mainScreen = treeLine([{ name: "Select a thread", role: "text", tag: 118 }, newThreadButton, logBoxToast]);
const toastNames = ["8", "Failed to initialize devtools client"];
const refusal = `${JSON.stringify({ error: "busy", ok: false })}\n`;
const FIRST_ANSWER = 0;
const NEXT_ANSWER = 1;
const NO_TRACE = "";
const NO_DELAY_MS = 0;
const LATE_ANSWER_MS = 80;

const watchTrace = (trace: string, isClosed = false): StartupWatch => ({
  isClosed: (): boolean => isClosed,
  read: (): string => trace,
});

/** How a fake window answers: after `delayMs`, and calling `onRequest`, if any, as each request arrives. */
interface AnswerTiming {
  readonly delayMs: number;
  readonly onRequest: (() => void) | null;
}

const promptly: AnswerTiming = { delayMs: NO_DELAY_MS, onRequest: null };

/** A window whose automation channel answers each request with the next of `answers`, the last one repeated. */
const serveAnswers = async (
  socketPath: string,
  answers: readonly string[],
  { delayMs, onRequest } = promptly,
): Promise<ReturnType<typeof createServer>> => {
  let served = FIRST_ANSWER;
  const server = createServer((socket) => {
    socket.once("data", () => {
      const answer = answers[Math.min(served, answers.length - NEXT_ANSWER)] ?? "";

      served += NEXT_ANSWER;
      onRequest?.();
      setTimeout(() => socket.end(answer), delayMs);
    });
  });

  server.listen(socketPath);
  await once(server, "listening");

  return server;
};

/** A wait in progress against a window that shows its main screen on its third poll, and whether it has returned. */
interface SettlingWait {
  readonly clock: VirtualClock;
  readonly pending: Promise<StartupOutcome>;
  readonly progress: { hasReturned: boolean };
  readonly server: ReturnType<typeof createServer>;
}

const startSettlingWait = async (socketPath: string): Promise<SettlingWait> => {
  const server = await serveAnswers(socketPath, [refusal, treeLine([logBoxToast]), mainScreen]);
  const lines = [`[rnl-automation] listening on ${socketPath}`, runningMain];
  const clock = createVirtualClock(SETTLE_MS);
  const progress = { hasReturned: false };
  const watch = { isClosed: (): boolean => false, read: (): string => lines.join("\n") };
  const pending = (async (): Promise<StartupOutcome> => {
    const outcome = await waitForMainScreen(watch, VIRTUAL_LAUNCH_MS, { ...timing, ...clock });

    progress.hasReturned = true;

    return outcome;
  })();

  return { clock, pending, progress, server };
};

/** A fresh directory per test for the fake window's automation socket, answering the socket's path. */
const useSocketPath = (): (() => string) => {
  let directory = "";

  beforeEach(() => {
    directory = mkdtempSync(path.join(tmpdir(), "rnl-t3code-startup-"));
  });

  afterEach(() => {
    rmSync(directory, { force: true, recursive: true });
  });

  return () => path.join(directory, "automation.sock");
};

describe("waitForMainScreen", () => {
  const socketPathOf = useSocketPath();

  it("polls through a refusal and LogBox's toast alone until the New thread button shows, and settles past it", async () => {
    const { clock, pending, progress, server } = await startSettlingWait(socketPathOf());

    await clock.requested;
    await setImmediate();
    const hasReturnedWhileSettling = progress.hasReturned;

    clock.release();
    const outcome = await pending;

    server.close();
    expect([hasReturnedWhileSettling, outcome, clock.sleeps, clock.now() - VIRTUAL_LAUNCH_MS]).toStrictEqual([
      false,
      {
        lastNames: ["Select a thread", "New thread", ...toastNames],
        mainScreenMs: MAIN_SCREEN_AFTER_MS,
        runningMainMs: NO_ELAPSED_MS,
      },
      [timing.pollIntervalMs, timing.pollIntervalMs, SETTLE_MS],
      MAIN_SCREEN_AFTER_MS + SETTLE_MS,
    ]);
  });

  it("times out on a window that shows LogBox's toast alone, and names what that toast held", async () => {
    const socketPath = socketPathOf();
    const server = await serveAnswers(socketPath, [treeLine([logBoxToast])]);
    const clock = createVirtualClock();
    const watch = watchTrace(`[rnl-automation] listening on ${socketPath}\n${runningMain}`);
    const outcome = await waitForMainScreen(watch, VIRTUAL_LAUNCH_MS, { ...virtualTiming, ...clock });

    server.close();
    expect([outcome, clock.now() - VIRTUAL_LAUNCH_MS]).toStrictEqual([
      { lastNames: toastNames, mainScreenMs: null, runningMainMs: NO_ELAPSED_MS },
      virtualTiming.timeoutMs,
    ]);
  });
});

describe("waitForMainScreen without a main screen to wait for", () => {
  it("stops at once when startup reports an error or the window closes, still timing a main run seen in that poll", async () => {
    expect([
      await waitForMainScreen(watchTrace(`${runningMain}\n${statusBarError}`), Date.now()),
      await waitForMainScreen(watchTrace(runningMain, true), Date.now()),
    ]).toStrictEqual([
      { lastNames: [], mainScreenMs: null, runningMainMs: expect.any(Number) },
      { lastNames: [], mainScreenMs: null, runningMainMs: expect.any(Number) },
    ]);
  });

  it("gives up after its timeout while the window has opened no automation channel", async () => {
    const clock = createVirtualClock();

    expect(
      await waitForMainScreen(watchTrace(NO_TRACE), VIRTUAL_LAUNCH_MS, { ...virtualTiming, ...clock }),
    ).toStrictEqual({ lastNames: [], mainScreenMs: null, runningMainMs: null });
  });
});

describe("waitForMainScreen around one poll", () => {
  const socketPathOf = useSocketPath();

  it("counts a main screen whose answer arrives after the deadline as a timeout", async () => {
    const socketPath = socketPathOf();
    const server = await serveAnswers(socketPath, [mainScreen], {
      delayMs: LATE_ANSWER_MS,
      onRequest: null,
    });
    const trace = `[rnl-automation] listening on ${socketPath}`;
    const result = await waitForMainScreen(watchTrace(trace), Date.now(), shortTiming);

    server.close();
    expect(result).toStrictEqual({
      lastNames: ["Select a thread", "New thread", ...toastNames],
      mainScreenMs: null,
      runningMainMs: null,
    });
  });

  it("times a main run printed during the poll that shows the main screen before settling past it", async () => {
    const socketPath = socketPathOf();
    const lines = [`[rnl-automation] listening on ${socketPath}`];
    const server = await serveAnswers(socketPath, [mainScreen], {
      delayMs: NO_DELAY_MS,
      onRequest: () => lines.push(runningMain),
    });
    const watch = { isClosed: (): boolean => false, read: (): string => lines.join("\n") };
    const outcome = await waitForMainScreen(watch, VIRTUAL_LAUNCH_MS, { ...timing, ...createVirtualClock() });

    server.close();
    expect([outcome.mainScreenMs, outcome.runningMainMs]).toStrictEqual([NO_ELAPSED_MS, NO_ELAPSED_MS]);
  });
});

describe("describeStartupTimings", () => {
  it("states how long main and the main screen took, or that one was not seen", () => {
    expect(describeStartupTimings({ lastNames: [], mainScreenMs: null, runningMainMs: 96_400 })).toBe(
      'time to "main": 96.4 s, time to the main screen: not seen',
    );
  });
});
