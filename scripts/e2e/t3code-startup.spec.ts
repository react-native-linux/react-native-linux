import { afterEach, beforeEach, describe, expect, it } from "vitest";
import { describeStartupTimings, findStartupErrors, waitForFirstCommit } from "./t3code-startup.ts";
import { mkdtempSync, rmSync } from "node:fs";
import type { StartupWatch } from "./t3code-startup.ts";
import { createServer } from "node:net";
import { once } from "node:events";
import path from "node:path";
import { tmpdir } from "node:os";

const statusBarError =
  "[console.error] Invariant Violation: TurboModuleRegistry.getEnforcing(...): 'StatusBarManager' could not be found.";
const componentError = "[console.error] TypeError: Cannot read property 'default' of undefined";
const fatalError = "[js-error] fatal Error: Cannot find native module 'ExpoUpdates'";
const runningMain = 'Running "main" with {"rootTag":1}';
const SETTLE_MS = 50;
const timing = { pollIntervalMs: 1, settleMs: SETTLE_MS, timeoutMs: 5000 };
const shortTiming = { pollIntervalMs: 1, settleMs: SETTLE_MS, timeoutMs: 20 };

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

const treeLine = (children: readonly unknown[]): string =>
  `${JSON.stringify({ ok: true, result: { roots: [{ children, tag: 1 }] } })}\n`;
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

describe("waitForFirstCommit", () => {
  const socketPathOf = useSocketPath();

  it("polls through a refusal and an empty root until a view commits, times main, and settles past the commit", async () => {
    const socketPath = socketPathOf();
    const server = await serveAnswers(socketPath, [refusal, treeLine([]), treeLine([{ tag: 2 }])]);
    const lines = [`[rnl-automation] listening on ${socketPath}`];
    const launchedAt = Date.now();
    const pending = waitForFirstCommit({ isClosed: () => false, read: () => lines.join("\n") }, launchedAt, timing);

    lines.push(runningMain);
    const { firstCommitMs, runningMainMs } = await pending;

    server.close();
    expect([
      runningMainMs === null,
      firstCommitMs !== null && Date.now() - launchedAt >= firstCommitMs + SETTLE_MS,
    ]).toStrictEqual([false, true]);
  });

  it("stops at once when startup reports an error or the window closes, still timing a main run seen in that poll", async () => {
    expect([
      await waitForFirstCommit(watchTrace(`${runningMain}\n${statusBarError}`), Date.now()),
      await waitForFirstCommit(watchTrace(runningMain, true), Date.now()),
    ]).toStrictEqual([
      { firstCommitMs: null, runningMainMs: expect.any(Number) },
      { firstCommitMs: null, runningMainMs: expect.any(Number) },
    ]);
  });

  it("gives up after its timeout while the window has opened no automation channel", async () => {
    expect(await waitForFirstCommit(watchTrace(NO_TRACE), Date.now(), shortTiming)).toStrictEqual({
      firstCommitMs: null,
      runningMainMs: null,
    });
  });
});

describe("waitForFirstCommit around one poll", () => {
  const socketPathOf = useSocketPath();

  it("counts a commit whose answer arrives after the deadline as a timeout", async () => {
    const socketPath = socketPathOf();
    const server = await serveAnswers(socketPath, [treeLine([{ tag: 2 }])], {
      delayMs: LATE_ANSWER_MS,
      onRequest: null,
    });
    const trace = `[rnl-automation] listening on ${socketPath}`;
    const result = await waitForFirstCommit(watchTrace(trace), Date.now(), shortTiming);

    server.close();
    expect(result).toStrictEqual({ firstCommitMs: null, runningMainMs: null });
  });

  it("times a main run printed during the poll that commits before settling past it", async () => {
    const socketPath = socketPathOf();
    const lines = [`[rnl-automation] listening on ${socketPath}`];
    const server = await serveAnswers(socketPath, [treeLine([{ tag: 2 }])], {
      delayMs: NO_DELAY_MS,
      onRequest: () => lines.push(runningMain),
    });
    const watch = { isClosed: (): boolean => false, read: (): string => lines.join("\n") };
    const { firstCommitMs, runningMainMs } = await waitForFirstCommit(watch, Date.now(), timing);

    server.close();
    expect(runningMainMs !== null && firstCommitMs !== null && runningMainMs <= firstCommitMs).toBe(true);
  });
});

describe("describeStartupTimings", () => {
  it("states how long main and the first commit took, or that one was not seen", () => {
    expect(describeStartupTimings({ firstCommitMs: null, runningMainMs: 96_400 })).toBe(
      'time to "main": 96.4 s, time to the first commit: not seen',
    );
  });
});
