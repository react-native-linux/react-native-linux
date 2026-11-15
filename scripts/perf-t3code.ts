import { env, execPath } from "node:process";
import { existsSync, mkdirSync, readFileSync, rmSync, writeFileSync } from "node:fs";
import { isRecord, pollUntil } from "./perf-devtools.ts";
import { setTimeout as delay } from "node:timers/promises";
import { execFile } from "node:child_process";
import path from "node:path";
import { promisify } from "node:util";

/*
 * The T3 Code side of scripts/perf-electron.ts: where a checkout keeps its build, the state the app starts from, and
 * the workload. Nothing needs an account: turns come from the replay agent T3 Code ships for its own UI checks.
 */

const TURN_TIMEOUT_MS = 120_000;
const SETTLE_MS = 2000;
/** Replayed before the measured turn so the feed is long enough to scroll. */
const WARM_TURN_MESSAGES = ["Warm turn 1", "Warm turn 2", "Warm turn 3"];
const SCROLL_SPEED_PIXELS_PER_SECOND = 1500;
const MINIMUM_SCROLL_PIXELS = 1;
const READY_ATTEMPT_MS = 1000;

/** The fixture T3 Code's own desktop launch check opens: one project, two short threads, an older schema. */
const FIXTURE_PATH = "scripts/fixtures/desktop-launch/statev2.sqlite";
const FIXTURE_PROJECT_ID = "launch-check-project";
const FIXTURE_WORKSPACE = "/tmp/t3-launch-check-workspace";
const REPLAY_PROVIDER = "devReplay";
/** Thinking, a plan, tool calls and markdown, streamed in chunks; its one approval needs none in full-access mode. */
const REPLAY_SCENARIO = "showcase";
const THREAD_ITEM_SELECTOR = "li[data-thread-item]";
const APP_URL_PREFIX = "t3code:";

const runFile = promisify(execFile);

/** The part of a DevTools connection the workload drives. */
interface Page {
  readonly evaluate: (expression: string) => Promise<unknown>;
  readonly send: (method: string, parameters?: Record<string, unknown>) => Promise<unknown>;
}

interface T3Code {
  readonly desktopDirectory: string;
  readonly electronPath: string;
  readonly replayAgentPath: string;
  readonly root: string;
  readonly serverEntryPath: string;
}

interface Workload {
  readonly measure: (name: string, body: () => Promise<unknown>) => Promise<void>;
  readonly page: Page;
  readonly t3Home: string;
  readonly t3code: T3Code;
}

/** `ensure:electron` writes the executable's name into `path.txt`, as Electron's own npm package does. */
const resolveT3Code = (root: string): T3Code => {
  const electronPackage = path.join(root, "apps", "desktop", "node_modules", "electron");
  const executableNamePath = path.join(electronPackage, "path.txt");
  const executableName = existsSync(executableNamePath) ? readFileSync(executableNamePath, "utf8").trim() : "electron";

  return {
    desktopDirectory: path.join(root, "apps", "desktop"),
    electronPath: path.join(electronPackage, "dist", executableName),
    replayAgentPath: path.join(root, "apps", "server", "scripts", "dev-replay-agent.ts"),
    root,
    serverEntryPath: path.join(root, "apps", "server", "dist", "bin.mjs"),
  };
};

const describeMissingBuild = (t3code: T3Code): readonly string[] =>
  [t3code.electronPath, t3code.serverEntryPath, t3code.replayAgentPath]
    .filter((requiredPath) => !existsSync(requiredPath))
    .map((missingPath) => `${missingPath} is missing; run "vp i && vp run build:desktop" in ${t3code.root}`);

/**
 * The fixture's state, plus the replay agent T3 Code's dev server registers, registered here by hand: the bundled
 * backend skips it. Returns the cleanup for the fixture's workspace, which lives at a fixed path outside `t3Home`.
 */
const seedHome = (t3code: T3Code, t3Home: string): (() => void) => {
  const stateDirectory = path.join(t3Home, "userdata");
  const ownsWorkspace = !existsSync(FIXTURE_WORKSPACE);

  mkdirSync(stateDirectory, { recursive: true });
  mkdirSync(FIXTURE_WORKSPACE, { recursive: true });
  writeFileSync(path.join(stateDirectory, "statev2.sqlite"), readFileSync(path.join(t3code.root, FIXTURE_PATH)));
  writeFileSync(
    path.join(stateDirectory, "settings.json"),
    JSON.stringify({
      providerInstances: {
        [REPLAY_PROVIDER]: {
          config: { commandArgs: [t3code.replayAgentPath], commandPath: execPath, source: "local" },
          displayName: "Replay",
          driver: "acpRegistry",
        },
      },
    }),
  );

  return () => {
    if (ownsWorkspace) {
      rmSync(FIXTURE_WORKSPACE, { force: true, recursive: true });
    }
  };
};

/** Variables beside the Wayland ones: private state, no telemetry, no update check. */
const buildAppEnvironment = (t3Home: string): Record<string, string> => ({
  T3CODE_DISABLE_AUTO_UPDATE: "true",
  T3CODE_HOME: t3Home,
  T3CODE_TELEMETRY_ENABLED: "false",
  XDG_CONFIG_HOME: path.join(t3Home, "..", "config"),
  XDG_DATA_HOME: path.join(t3Home, "..", "data"),
});

/** T3 Code's own CLI against the app's backend, run by the Electron binary in Node mode as the app runs its backend. */
const runThreadCommand = async (workload: Workload, commandArguments: readonly string[]): Promise<string> => {
  const { stdout } = await runFile(
    workload.t3code.electronPath,
    [workload.t3code.serverEntryPath, "thread", ...commandArguments, "--base-dir", workload.t3Home],
    { env: { ...env, ELECTRON_RUN_AS_NODE: "1" }, timeout: TURN_TIMEOUT_MS },
  );

  return stdout.trim();
};

/**
 * Resolves once the sidebar lists the fixture's threads, the first usable state. A MutationObserver reports the
 * list within one DevTools round trip of React committing it; the window reloads once on the way, which destroys
 * the context mid-evaluate, so each attempt waits a second at most and the next one reads the new document.
 */
const waitForThreadList = async (page: Page, timeoutMilliseconds: number): Promise<void> => {
  const isListed = async (): Promise<true | null> => {
    try {
      const value = await page.evaluate(
        `new Promise((resolve) => {
          const isListed = () => document.querySelector(${JSON.stringify(THREAD_ITEM_SELECTOR)}) !== null;
          const observer = new MutationObserver(() => { if (isListed()) { observer.disconnect(); resolve(true); } });
          observer.observe(document, { childList: true, subtree: true });
          setTimeout(() => { observer.disconnect(); resolve(isListed()); }, ${String(READY_ATTEMPT_MS)});
          if (isListed()) { observer.disconnect(); resolve(true); }
        })`,
      );

      return value === true ? true : null;
    } catch {
      return null;
    }
  };

  if ((await pollUntil(isListed, timeoutMilliseconds)) === null) {
    throw new Error("the sidebar never listed the fixture's threads");
  }
};

/** The thread's route is `#/<environment>/<thread>`; the backend writes its environment id into its state directory. */
const openThread = async (workload: Workload, threadId: string): Promise<void> => {
  const environmentId = readFileSync(path.join(workload.t3Home, "userdata", "environment-id"), "utf8").trim();

  await workload.page.evaluate(`location.hash = ${JSON.stringify(`#/${environmentId}/${threadId}`)}`);
};

/**
 * `Input.synthesizeScrollGesture` parameters for the feed: the scrollable element with the most content past the
 * fold, scrolled from the bottom, where a finished turn leaves it, all the way to the top.
 */
const findFeedScrollGesture = async (page: Page): Promise<Record<string, unknown>> => {
  const gesture = await page.evaluate(`(() => {
      const scrollable = (element) => element.scrollHeight - element.clientHeight;
      const feed = [...document.querySelectorAll("*")]
        .filter((element) => /auto|scroll/.test(getComputedStyle(element).overflowY))
        .sort((left, right) => scrollable(right) - scrollable(left))[0];
      const box = feed.getBoundingClientRect();
      return { x: box.x + box.width / 2, y: box.y + box.height / 2, yDistance: scrollable(feed) };
    })()`);

  if (!isRecord(gesture) || Number(gesture["yDistance"]) < MINIMUM_SCROLL_PIXELS) {
    throw new Error("the thread feed has nothing to scroll");
  }

  return { ...gesture, gestureSourceType: "mouse", speed: SCROLL_SPEED_PIXELS_PER_SECOND };
};

/** Up the whole feed and back down, as wheel events Chromium paces itself. */
const scrollFeed = async (page: Page, gesture: Record<string, unknown>): Promise<void> => {
  await page.send("Input.synthesizeScrollGesture", gesture);
  await page.send("Input.synthesizeScrollGesture", { ...gesture, yDistance: -Number(gesture["yDistance"]) });
};

/** A few replayed turns fill the feed; then one more is measured while it streams, then the feed is scrolled. */
const driveWorkload = async (workload: Workload): Promise<void> => {
  const startArguments = ["start", "--provider", REPLAY_PROVIDER, "--model", REPLAY_SCENARIO, FIXTURE_PROJECT_ID];
  const threadId = await runThreadCommand(workload, [...startArguments, "Warm the feed"]);

  await openThread(workload, threadId);
  for (const message of WARM_TURN_MESSAGES) {
    await runThreadCommand(workload, ["send", threadId, message, "--wait"]);
  }
  await delay(SETTLE_MS);
  await workload.measure("Streamed reply", async () => {
    await runThreadCommand(workload, ["send", threadId, "Measured turn", "--wait"]);
  });
  await delay(SETTLE_MS);

  const gesture = await findFeedScrollGesture(workload.page);

  await workload.measure("Feed scroll", () => scrollFeed(workload.page, gesture));
};

export {
  APP_URL_PREFIX,
  buildAppEnvironment,
  describeMissingBuild,
  driveWorkload,
  resolveT3Code,
  seedHome,
  waitForThreadList,
};
