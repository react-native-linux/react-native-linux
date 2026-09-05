import { afterEach, beforeEach, describe, expect, it } from "vitest";
import { existsSync, mkdtempSync, readFileSync, rmSync, writeFileSync } from "node:fs";
import { gradeAutomation, requestAutomation } from "./automation.ts";

import type { ScenarioAutomation } from "./scenario.ts";
import { createServer } from "node:net";
import { once } from "node:events";
import path from "node:path";
import { tmpdir } from "node:os";

const CONNECT_TIMEOUT_MS = 2000;
const HANG_MS = 1500;
const FIRST_FAILURE = 0;
const ONE_FAILURE = 1;
const NO_TEXT = "";
const SNAPSHOT_NAME = "tree.json";
const TREE_ARTIFACT = "automation-tree.json";
const SCREENSHOT_ARTIFACT = "automation-screenshot.png";

type Answers = Readonly<Record<string, string>>;

interface FakeWindow {
  readonly answers: Answers;
  readonly writesPicture: boolean;
}

const isRecord = (value: unknown): value is Record<string, unknown> => typeof value === "object" && value !== null;

const okLine = (command: string, result: Record<string, unknown>): string =>
  `${JSON.stringify({ command, ok: true, result })}\n`;

const refusalLine = (reason: string): string => `${JSON.stringify({ error: reason, ok: false })}\n`;

const MOUNTED_CHILD = { componentName: "View", testID: "box" };

/**
 * What a healthy window answers. A command missing from a scenario's own table falls through to this one, and a
 * command missing from both is answered with nothing at all — which is how `HangForTesting` blocking is
 * simulated, because the client's only evidence of a hang is a deadline that passed.
 */
const healthyAnswers: Answers = {
  DumpVisualTree: okLine("DumpVisualTree", { roots: [{ children: [MOUNTED_CHILD], componentName: "RootView" }] }),
  ListErrors: okLine("ListErrors", { errors: [] }),
  MarkTestPassed: okLine("MarkTestPassed", { passed: true }),
  TakeScreenshot: okLine("TakeScreenshot", { path: "shot.png" }),
};

const EVERY_COMMAND: ScenarioAutomation = {
  listErrorsMustBeEmpty: true,
  markTestPassed: true,
  visualTreeSnapshot: SNAPSHOT_NAME,
};

const CHANNEL_ONLY: ScenarioAutomation = {
  listErrorsMustBeEmpty: false,
  markTestPassed: false,
  visualTreeSnapshot: null,
};

let directory = NO_TEXT;
let socketPath = NO_TEXT;
let server: ReturnType<typeof createServer> | null = null;
const asked: string[] = [];

const answerFor = (chunk: Buffer, window: FakeWindow): string => {
  const request: unknown = JSON.parse(chunk.toString().trim());
  const command = isRecord(request) ? String(request["command"]) : NO_TEXT;

  asked.push(command);

  if (command === "TakeScreenshot" && window.writesPicture && isRecord(request)) {
    writeFileSync(String(request["path"]), NO_TEXT);
  }

  return window.answers[command] ?? healthyAnswers[command] ?? NO_TEXT;
};

/** A stand-in window: a real line-delimited JSON socket, so the client is exercised rather than mocked. */
const startWindow = async (window: FakeWindow): Promise<void> => {
  server = createServer((socket) => {
    socket.on("data", (chunk: Buffer) => {
      socket.write(answerFor(chunk, window));
    });
  });
  server.listen(socketPath);
  await once(server, "listening");
};

const gradeAgainstWindow = (automation: ScenarioAutomation): Promise<readonly string[]> =>
  gradeAutomation({
    artifactsDirectory: directory,
    automation,
    goldensDirectory: directory,
    trace: `[rnl-automation] listening on ${socketPath}\n`,
  });

const grade = async (answers: Answers, automation: ScenarioAutomation): Promise<readonly string[]> => {
  await startWindow({ answers, writesPicture: true });

  return gradeAgainstWindow(automation);
};

const blessSnapshot = (children: readonly unknown[]): void => {
  writeFileSync(path.join(directory, SNAPSHOT_NAME), JSON.stringify(children));
};

beforeEach(() => {
  directory = mkdtempSync(path.join(tmpdir(), "rnl-automation-"));
  socketPath = path.join(directory, "channel.sock");
  asked.length = FIRST_FAILURE;
});

afterEach(() => {
  server?.close();
  server = null;
  rmSync(directory, { force: true, recursive: true });
});

describe("one request, one answer", () => {
  it("reads the one line that answers a request", async () => {
    await startWindow({ answers: {}, writesPicture: true });

    const answer = await requestAutomation(socketPath, { command: "ListErrors" }, CONNECT_TIMEOUT_MS);

    expect(answer.result).toEqual({ errors: [] });
  });

  it("reports a socket nothing is listening on", async () => {
    const answer = await requestAutomation(socketPath, { command: "ListErrors" }, CONNECT_TIMEOUT_MS);

    expect(answer.failure).toContain("ListErrors did not answer");
  });

  it("fails a run whose window never printed the socket path", async () => {
    const failures = await gradeAutomation({
      artifactsDirectory: directory,
      automation: EVERY_COMMAND,
      goldensDirectory: directory,
      trace: "boot\n",
    });

    expect(failures).toEqual(["the window never printed the automation socket path"]);
  });
});

describe("a window that answers everything", () => {
  it("produces no failures", async () => {
    blessSnapshot([MOUNTED_CHILD]);

    expect(await grade({}, EVERY_COMMAND)).toEqual([]);
  });

  it("writes the observed tree beside the run", async () => {
    blessSnapshot([MOUNTED_CHILD]);
    await grade({}, EVERY_COMMAND);

    const observed: unknown = JSON.parse(readFileSync(path.join(directory, TREE_ARTIFACT), "utf8"));

    expect(observed).toEqual([MOUNTED_CHILD]);
  });

  it("asks only the two commands that need no flag when the scenario sets none", async () => {
    await grade({}, CHANNEL_ONLY);

    expect(asked).toEqual(["TakeScreenshot", "HangForTesting"]);
  });
});

describe("ListErrors", () => {
  const onlyErrors: ScenarioAutomation = { ...CHANNEL_ONLY, listErrorsMustBeEmpty: true };

  it("fails when the runtime reported anything", async () => {
    const reported = { errors: [{ message: "boom", source: "javascript" }] };
    const failures = await grade({ ListErrors: okLine("ListErrors", reported) }, onlyErrors);

    expect(failures[FIRST_FAILURE]).toContain("ListErrors reported 1 error(s)");
  });

  it("fails when the window refuses it", async () => {
    const failures = await grade({ ListErrors: refusalLine("nope") }, onlyErrors);

    expect(failures[FIRST_FAILURE]).toBe("nope");
  });
});

describe("DumpVisualTree", () => {
  const onlyTree: ScenarioAutomation = { ...CHANNEL_ONLY, visualTreeSnapshot: SNAPSHOT_NAME };

  it("fails when the tree does not match its snapshot", async () => {
    blessSnapshot([{ componentName: "View", testID: "other" }]);

    const failures = await grade({}, onlyTree);

    expect(failures[FIRST_FAILURE]).toContain(`the visual tree does not match ${SNAPSHOT_NAME}`);
  });

  it("fails when there is no snapshot to compare against yet", async () => {
    const failures = await grade({}, { ...CHANNEL_ONLY, visualTreeSnapshot: "missing.json" });

    expect(failures[FIRST_FAILURE]).toContain("there is no visual-tree snapshot");
  });

  it("fails when the window refuses it", async () => {
    const failures = await grade({ DumpVisualTree: refusalLine("no bundle is running") }, onlyTree);

    expect(failures[FIRST_FAILURE]).toBe("no bundle is running");
  });
});

describe("MarkTestPassed", () => {
  const onlyMark: ScenarioAutomation = { ...CHANNEL_ONLY, markTestPassed: true };

  it("fails when the bundle never marked itself passed", async () => {
    const failures = await grade({ MarkTestPassed: okLine("MarkTestPassed", { passed: false }) }, onlyMark);

    expect(failures[FIRST_FAILURE]).toBe("the bundle never called globalThis.__rnlMarkTestPassed()");
  });

  it("fails when the window refuses it", async () => {
    const failures = await grade({ MarkTestPassed: refusalLine("no bundle is running") }, onlyMark);

    expect(failures[FIRST_FAILURE]).toBe("no bundle is running");
  });
});

describe("the channel itself", () => {
  it("fails when TakeScreenshot answers but writes no picture", async () => {
    await startWindow({ answers: {}, writesPicture: false });

    const failures = await gradeAgainstWindow(CHANNEL_ONLY);

    expect(failures[FIRST_FAILURE]).toContain("wrote no picture");
    expect(existsSync(path.join(directory, SCREENSHOT_ARTIFACT))).toBe(false);
  });

  it("fails when TakeScreenshot is refused", async () => {
    const failures = await grade({ TakeScreenshot: refusalLine("no path") }, CHANNEL_ONLY);

    expect(failures[FIRST_FAILURE]).toBe("no path");
  });

  it("fails when HangForTesting answers instead of blocking", async () => {
    const answered = okLine("HangForTesting", { milliseconds: HANG_MS });
    const failures = await grade({ HangForTesting: answered }, CHANNEL_ONLY);

    expect(failures.filter((failure) => failure.includes("never blocked"))).toHaveLength(ONE_FAILURE);
  });
});
