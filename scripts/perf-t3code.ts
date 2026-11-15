import { buildWheelSteps, checkScrollTravel, formatScrollWorkload } from "./perf/scroll.ts";
import { env, execPath, hrtime } from "node:process";
import { existsSync, mkdirSync, readFileSync, rmSync, writeFileSync } from "node:fs";
import { setTimeout as delay } from "node:timers/promises";
import { execFile } from "node:child_process";
import { injectorBinaryPath } from "./e2e-processes.ts";
import { isRecord } from "./e2e/fields.ts";
import path from "node:path";
import { pollUntil } from "./perf-devtools.ts";
import { promisify } from "node:util";

/*
 * The T3 Code side of scripts/perf-electron.ts: where a checkout keeps its build, the state the app starts from, and
 * the workload. Nothing needs an account: turns come from the replay agent T3 Code ships for its own UI checks.
 */

const TURN_TIMEOUT_MS = 120_000;
const SETTLE_MS = 2000;
/** Replayed before the measured turn so the feed is long enough to scroll. */
const WARM_TURN_MESSAGES = ["Warm turn 1", "Warm turn 2", "Warm turn 3"];
const READY_ATTEMPT_MS = 1000;
const NANOSECONDS_PER_MILLISECOND = 1_000_000;

/** The fixture T3 Code's own desktop launch check opens: one project, two short threads, an older schema. */
const FIXTURE_PATH = "scripts/fixtures/desktop-launch/statev2.sqlite";
const FIXTURE_PROJECT_ID = "launch-check-project";
const FIXTURE_WORKSPACE = "/tmp/t3-launch-check-workspace";
const REPLAY_PROVIDER = "devReplay";
/** Thinking, a plan, tool calls and markdown, streamed in chunks; its one approval needs none in full-access mode. */
const REPLAY_SCENARIO = "showcase";
const THREAD_ITEM_SELECTOR = "li[data-thread-item]";
/** The thread timeline's scroll container, LegendList's element in `MessagesTimeline`. */
const FEED_SELECTOR = ".messages-timeline-scroll";
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

type Measure = <Value>(name: string, body: () => Promise<Value>) => Promise<Value>;

interface Workload {
  readonly display: { readonly runtimeDirectory: string; readonly socketName: string };
  readonly measure: Measure;
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
 * Observed readiness on `CLOCK_MONOTONIC`: when this harness first saw the sidebar list the fixture's threads. In
 * the page a MutationObserver sees React commit the list, and two animation frames later the frame that holds it
 * has been produced; the check then replies with the time since it saw the list, which places that moment on this
 * process's clock to within the reply's latency. If the list is already there when a check begins (DevTools
 * attached late, or the commit fell between two checks) that moment is the check's, after the commit, and the
 * result says so. The window reloads once on the way, which destroys the context mid-evaluate, so each check waits
 * a second at most and the next one reads the new document.
 */
const waitForThreadList = async (
  page: Page,
  timeoutMilliseconds: number,
): Promise<{ nanoseconds: number; wasAlreadyPresent: boolean }> => {
  const check = async (): Promise<{ nanoseconds: number; wasAlreadyPresent: boolean } | null> => {
    try {
      const listed = await page.evaluate(
        `new Promise((resolve) => {
          const isListed = () => document.querySelector(${JSON.stringify(THREAD_ITEM_SELECTOR)}) !== null;
          let listedAt = null;
          const resolveAfterFrame = (wasAlreadyPresent) => {
            listedAt = performance.now();
            observer.disconnect();
            requestAnimationFrame(() => requestAnimationFrame(() =>
              resolve({ millisecondsSinceListed: performance.now() - listedAt, wasAlreadyPresent })));
          };
          const observer = new MutationObserver(() => { if (listedAt === null && isListed()) resolveAfterFrame(false); });
          observer.observe(document, { childList: true, subtree: true });
          setTimeout(() => { if (listedAt === null) { observer.disconnect(); resolve(null); } }, ${String(READY_ATTEMPT_MS)});
          if (isListed()) resolveAfterFrame(true);
        })`,
      );

      return isRecord(listed)
        ? {
            nanoseconds:
              Number(hrtime.bigint()) - Number(listed["millisecondsSinceListed"]) * NANOSECONDS_PER_MILLISECOND,
            wasAlreadyPresent: listed["wasAlreadyPresent"] === true,
          }
        : null;
    } catch {
      return null;
    }
  };
  const readiness = await pollUntil(check, timeoutMilliseconds);

  if (readiness === null) {
    throw new Error("the sidebar never listed the fixture's threads");
  }

  return readiness;
};

/** The thread's route is `#/<environment>/<thread>`; the backend writes its environment id into its state directory. */
const openThread = async (workload: Workload, threadId: string): Promise<void> => {
  const environmentId = readFileSync(path.join(workload.t3Home, "userdata", "environment-id"), "utf8").trim();

  await workload.page.evaluate(`location.hash = ${JSON.stringify(`#/${environmentId}/${threadId}`)}`);
};

/** The feed at its bottom, where a finished turn leaves it, and the output point at its centre. */
const prepareFeed = async (page: Page): Promise<{ bottom: number; xPixels: number; yPixels: number }> => {
  const feedExpression = `document.querySelector(${JSON.stringify(FEED_SELECTOR)})`;

  await page.evaluate(`(() => { const feed = ${feedExpression}; feed.scrollTop = feed.scrollHeight; })()`);
  await delay(SETTLE_MS);

  const feed = await page.evaluate(`(() => {
      const feed = ${feedExpression};
      const box = feed.getBoundingClientRect();
      const top = outerHeight - innerHeight;
      return {
        bottom: feed.scrollHeight - feed.clientHeight,
        xPixels: box.x + box.width / 2,
        yPixels: top + box.y + box.height / 2,
      };
    })()`);

  if (!isRecord(feed)) {
    throw new Error(`the thread has no ${FEED_SELECTOR} to scroll`);
  }

  return { bottom: Number(feed["bottom"]), xPixels: Number(feed["xPixels"]), yPixels: Number(feed["yPixels"]) };
};

const readScrollTop = async (page: Page): Promise<number> =>
  Number(await page.evaluate(`document.querySelector(${JSON.stringify(FEED_SELECTOR)}).scrollTop`));

/**
 * Runs rnl_inject asynchronously: a synchronous child would stop this process draining Electron's WAYLAND_DEBUG
 * pipe, and Electron blocks on the full pipe for as long as the child runs.
 */
const injectWheel = async (workload: Workload, steps: readonly string[]): Promise<number> => {
  const scriptPath = path.join(workload.t3Home, "..", "wheel.txt");

  writeFileSync(scriptPath, `${steps.join("\n")}\n`);
  await runFile(injectorBinaryPath, [scriptPath], {
    env: {
      ...env,
      WAYLAND_DISPLAY: workload.display.socketName,
      XDG_RUNTIME_DIR: workload.display.runtimeDirectory,
      XKB_DEFAULT_LAYOUT: "us",
    },
    timeout: TURN_TIMEOUT_MS,
  });

  return readScrollTop(workload.page);
};

/** The shared wheel workload, each direction its own phase, and its travel checked; returns its description. */
const scrollFeed = async (workload: Workload): Promise<string> => {
  const { bottom, ...point } = await prepareFeed(workload.page);
  const start = await readScrollTop(workload.page);
  const top = await workload.measure("Scroll up", () => injectWheel(workload, buildWheelSteps(point, "up")));
  const end = await workload.measure("Scroll down", () => injectWheel(workload, buildWheelSteps(point, "down")));
  const travel = checkScrollTravel({ bottom, end, start, top });

  if (typeof travel === "string") {
    throw new TypeError(travel);
  }

  return formatScrollWorkload(travel);
};

/** A few replayed turns fill the feed; then one more is measured while it streams, then the feed is scrolled. */
const driveWorkload = async (workload: Workload): Promise<string> => {
  const startArguments = ["start", "--provider", REPLAY_PROVIDER, "--model", REPLAY_SCENARIO, FIXTURE_PROJECT_ID];
  const threadId = await runThreadCommand(workload, [...startArguments, "Warm the feed"]);

  await openThread(workload, threadId);
  for (const message of WARM_TURN_MESSAGES) {
    await runThreadCommand(workload, ["send", threadId, message, "--wait"]);
  }
  await delay(SETTLE_MS);
  await workload.measure("Streamed reply", () =>
    runThreadCommand(workload, ["send", threadId, "Measured turn", "--wait"]),
  );
  await delay(SETTLE_MS);

  return scrollFeed(workload);
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
