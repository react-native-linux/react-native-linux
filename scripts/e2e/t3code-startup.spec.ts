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
const timing = { pollIntervalMs: 1, timeoutMs: 5000 };
const shortTiming = { pollIntervalMs: 1, timeoutMs: 20 };

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

const watchTrace = (trace: string, isClosed = false): StartupWatch => ({
  isClosed: (): boolean => isClosed,
  read: (): string => trace,
});

/** A window whose automation channel answers each request with the next of `answers`, the last one repeated. */
const serveAnswers = async (
  socketPath: string,
  answers: readonly string[],
): Promise<ReturnType<typeof createServer>> => {
  let served = FIRST_ANSWER;
  const server = createServer((socket) => {
    socket.once("data", () => {
      socket.end(answers[Math.min(served, answers.length - NEXT_ANSWER)] ?? "");
      served += NEXT_ANSWER;
    });
  });

  server.listen(socketPath);
  await once(server, "listening");

  return server;
};

describe("waitForFirstCommit", () => {
  let directory = "";

  beforeEach(() => {
    directory = mkdtempSync(path.join(tmpdir(), "rnl-t3code-startup-"));
  });

  afterEach(() => {
    rmSync(directory, { force: true, recursive: true });
  });

  it("polls through a refusal and an empty root until a view commits, and times when main ran", async () => {
    const socketPath = path.join(directory, "automation.sock");
    const server = await serveAnswers(socketPath, [refusal, treeLine([]), treeLine([{ tag: 2 }])]);
    const lines = [`[rnl-automation] listening on ${socketPath}`];
    const pending = waitForFirstCommit({ isClosed: () => false, read: () => lines.join("\n") }, Date.now(), timing);

    lines.push(runningMain);
    const { firstCommitMs, runningMainMs } = await pending;

    server.close();
    expect([firstCommitMs === null, runningMainMs === null]).toStrictEqual([false, false]);
  });

  it("stops at once when startup reports an error or the window closes", async () => {
    expect([
      await waitForFirstCommit(watchTrace(`${runningMain}\n${statusBarError}`), Date.now()),
      await waitForFirstCommit(watchTrace(runningMain, true), Date.now()),
    ]).toStrictEqual([
      { firstCommitMs: null, runningMainMs: null },
      { firstCommitMs: null, runningMainMs: null },
    ]);
  });

  it("gives up after its timeout while the window has opened no automation channel", async () => {
    expect(await waitForFirstCommit(watchTrace(NO_TRACE), Date.now(), shortTiming)).toStrictEqual({
      firstCommitMs: null,
      runningMainMs: null,
    });
  });
});

describe("describeStartupTimings", () => {
  it("states how long main and the first commit took, or that one was not seen", () => {
    expect(describeStartupTimings({ firstCommitMs: null, runningMainMs: 96_400 })).toBe(
      'time to "main": 96.4 s, time to the first commit: not seen',
    );
  });
});
