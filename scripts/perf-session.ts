import type { AppRun, MeasuredPhase } from "./perf/report.ts";
import { classifyProcessTree, readProcessSamples, summarizeTreeUsage } from "./perf/processes.ts";
import { execFileSync, spawn } from "node:child_process";
import { readFileSync, readdirSync } from "node:fs";
import { buildEnvironment } from "./headless-compositor.ts";
import { connectToPage } from "./perf-devtools.ts";
import { setTimeout as delay } from "node:timers/promises";
import { hrtime } from "node:process";
import { waitUntil } from "./e2e/wait-until.ts";

/*
 * One run of an Electron app inside the compositor: the process, its WAYLAND_DEBUG trace, and the phases measured
 * against it. What the app is, when it is ready and what it does come from the `AppLaunch`. Every process the app
 * started is killed on the way out.
 */

const STARTUP_TIMEOUT_MS = 120_000;
const SETTLE_MS = 2000;
const STOP_GRACE_MS = 5000;
const TRACE_TAIL_LENGTH = 4000;
const NO_PID = 0;
const DEVTOOLS_PATTERN = /DevTools listening on ws:\/\/127\.0\.0\.1:(?<port>\d+)\//u;

type App = ReturnType<typeof spawn>;
type DevTools = Awaited<ReturnType<typeof connectToPage>>;
type Measure = (name: string, body: () => Promise<unknown>) => Promise<void>;
type RecordedPhases = Pick<AppRun, "phases" | "readyNanoseconds">;

interface AppLaunch {
  readonly arguments: readonly string[];
  readonly drive: (page: DevTools, measure: Measure) => Promise<void>;
  readonly environment: Readonly<Record<string, string>>;
  readonly executablePath: string;
  readonly pageUrlPrefix: string;
  /** Resolves at the app's first usable state, which ends the cold-start phase. */
  readonly waitForReady: (page: DevTools) => Promise<void>;
}

interface Display {
  readonly runtimeDirectory: string;
  readonly socketName: string;
}

interface Trace {
  text: string;
}

interface Started {
  readonly app: App;
  readonly spawnedNanoseconds: number;
  readonly trace: Trace;
}

const nowNanoseconds = (): number => Number(hrtime.bigint());

const sampleProcesses = (): ReturnType<typeof readProcessSamples> =>
  readProcessSamples(readdirSync("/proc"), (procPath) => readFileSync(procPath, "utf8"));

/** `--remote-debugging-port=0` picks a free port, which Electron prints on stderr. */
const startApp = (launch: AppLaunch, display: Display): App =>
  spawn(
    launch.executablePath,
    [...launch.arguments, "--no-sandbox", "--ozone-platform=wayland", "--remote-debugging-port=0"],
    {
      env: buildEnvironment({
        ...launch.environment,
        WAYLAND_DEBUG: "client",
        WAYLAND_DISPLAY: display.socketName,
        XDG_RUNTIME_DIR: display.runtimeDirectory,
      }),
      stdio: ["ignore", "pipe", "pipe"],
    },
  );

const readDevToolsPort = async (trace: Trace): Promise<string> => {
  await waitUntil(() => DEVTOOLS_PATTERN.test(trace.text), STARTUP_TIMEOUT_MS);

  const port = DEVTOOLS_PATTERN.exec(trace.text)?.groups?.["port"] ?? null;

  if (port === null) {
    throw new Error(`Electron never opened its DevTools endpoint:\n${trace.text.slice(-TRACE_TAIL_LENGTH)}`);
  }

  return port;
};

/** SIGTERM first so Electron stops its own children, then SIGKILL for anything in the tree still alive. */
const stopProcessTree = async (app: App): Promise<void> => {
  const treePids = [...classifyProcessTree(sampleProcesses(), app.pid ?? NO_PID).keys()];

  app.kill("SIGTERM");
  await waitUntil(() => app.exitCode !== null || app.signalCode !== null, STOP_GRACE_MS);
  for (const pid of treePids) {
    try {
      process.kill(pid, "SIGKILL");
    } catch {
      // Already gone.
    }
  }
};

/** Each measured phase is bracketed by its own /proc samples. */
const createMeasure =
  (phases: MeasuredPhase[], rootPid: number): Measure =>
  async (name, body) => {
    const before = sampleProcesses();
    const startNanoseconds = nowNanoseconds();

    await body();
    phases.push({
      endNanoseconds: nowNanoseconds(),
      name,
      startNanoseconds,
      usage: summarizeTreeUsage(before, sampleProcesses(), rootPid),
    });
  };

/** Cold start runs from spawn to the first usable state; the app idles, then runs its workload. */
const recordPhases = async (launch: AppLaunch, page: DevTools, started: Started): Promise<RecordedPhases> => {
  await launch.waitForReady(page);

  const rootPid = started.app.pid ?? NO_PID;
  const readyNanoseconds = nowNanoseconds();
  const usage = summarizeTreeUsage([], sampleProcesses(), rootPid);
  const phases: MeasuredPhase[] = [
    { endNanoseconds: readyNanoseconds, name: "Cold start", startNanoseconds: started.spawnedNanoseconds, usage },
  ];
  const measure = createMeasure(phases, rootPid);

  await delay(SETTLE_MS);
  await measure("Idle", () => delay(SETTLE_MS));
  await launch.drive(page, measure);

  return { phases, readyNanoseconds };
};

const connectAndRecord = async (launch: AppLaunch, started: Started): Promise<RecordedPhases> => {
  const page = await connectToPage(await readDevToolsPort(started.trace), launch.pageUrlPrefix, STARTUP_TIMEOUT_MS);

  return recordPhases(launch, page, started).finally(() => {
    page.close();
  });
};

/** The trace is read after the app has stopped, so it holds the presentation events of the last frames too. */
const runApp = async (launch: AppLaunch, display: Display): Promise<AppRun> => {
  const trace: Trace = { text: "" };
  const spawnedNanoseconds = nowNanoseconds();
  const app = startApp(launch, display);
  const record = (chunk: Buffer): void => {
    trace.text += chunk.toString();
  };

  app.stdout?.on("data", record);
  app.stderr?.on("data", record);

  const recorded = await connectAndRecord(launch, { app, spawnedNanoseconds, trace }).finally(() =>
    stopProcessTree(app),
  );
  const ticksPerSecond = Number(execFileSync("getconf", ["CLK_TCK"], { encoding: "utf8" }));

  return { ...recorded, spawnedNanoseconds, ticksPerSecond, trace: trace.text };
};

export { STARTUP_TIMEOUT_MS, runApp };
