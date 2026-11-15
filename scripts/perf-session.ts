import { APP_URL_PREFIX, buildAppEnvironment, driveWorkload, waitForThreadList } from "./perf-t3code.ts";
import type { AppRun, MeasuredPhase } from "./perf/report.ts";
import { classifyProcessTree, readProcessSamples, summarizeTreeUsage } from "./perf/processes.ts";
import { connectToPage, readGpuInfo } from "./perf-devtools.ts";
import { execFileSync, spawn } from "node:child_process";
import { readFileSync, readdirSync } from "node:fs";
import { buildEnvironment } from "./headless-compositor.ts";
import { setTimeout as delay } from "node:timers/promises";
import { hrtime } from "node:process";
import { waitUntil } from "./e2e/wait-until.ts";

/*
 * One run of T3 Code's Electron build inside the compositor: the process, its WAYLAND_DEBUG trace, and the phases
 * measured against it. Every process the app started is killed on the way out.
 */

const STARTUP_TIMEOUT_MS = 120_000;
const SETTLE_MS = 2000;
const STOP_GRACE_MS = 5000;
const TRACE_TAIL_LENGTH = 4000;
const NO_PID = 0;
const DEVTOOLS_PATTERN = /DevTools listening on ws:\/\/127\.0\.0\.1:(?<port>\d+)\//u;

type App = ReturnType<typeof spawn>;
type DevTools = Awaited<ReturnType<typeof connectToPage>>;
type ProcessSamples = ReturnType<typeof readProcessSamples>;
type Workload = Parameters<typeof driveWorkload>[number];

/** One rung: the switches that tell Electron how to draw, inside a compositor running its own renderer. */
interface ElectronRun {
  readonly display: { readonly compositorPid: number; readonly runtimeDirectory: string; readonly socketName: string };
  readonly electronArguments: readonly string[];
  readonly t3Home: string;
  readonly t3code: Workload["t3code"];
}

interface Trace {
  text: string;
}

interface Started {
  readonly app: App;
  readonly atSpawn: ProcessSamples;
  readonly run: ElectronRun;
  readonly spawnedNanoseconds: number;
  readonly trace: Trace;
}

const nowNanoseconds = (): number => Number(hrtime.bigint());

const sampleProcesses = (): ProcessSamples =>
  readProcessSamples(readdirSync("/proc"), (procPath) => readFileSync(procPath, "utf8"));

/** `--remote-debugging-port=0` picks a free port, which Electron prints on stderr. */
const startApp = (run: ElectronRun): App =>
  spawn(
    run.t3code.electronPath,
    [
      run.t3code.desktopDirectory,
      ...run.electronArguments,
      "--no-sandbox",
      "--ozone-platform=wayland",
      "--remote-debugging-port=0",
    ],
    {
      env: buildEnvironment({
        ...buildAppEnvironment(run.t3Home),
        WAYLAND_DEBUG: "client",
        WAYLAND_DISPLAY: run.display.socketName,
        XDG_RUNTIME_DIR: run.display.runtimeDirectory,
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
  const treePids = [
    ...classifyProcessTree(sampleProcesses(), { backendEntryPath: null, rootPid: app.pid ?? NO_PID }).keys(),
  ];

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

/** The app's tree split by role, and the compositor's whole tree, between two samples. */
const measurePhase = (
  started: Started,
  bounds: { before: ProcessSamples; name: string; startNanoseconds: number },
): MeasuredPhase => {
  const endNanoseconds = nowNanoseconds();
  const after = sampleProcesses();

  return {
    compositorUsage: summarizeTreeUsage(bounds.before, after, {
      backendEntryPath: null,
      rootPid: started.run.display.compositorPid,
    }),
    endNanoseconds,
    name: bounds.name,
    startNanoseconds: bounds.startNanoseconds,
    usage: summarizeTreeUsage(bounds.before, after, {
      backendEntryPath: started.run.t3code.serverEntryPath,
      rootPid: started.app.pid ?? NO_PID,
    }),
  };
};

/** Each measured phase is bracketed by its own /proc samples. */
const createMeasure =
  (phases: MeasuredPhase[], started: Started): Workload["measure"] =>
  async (name, body) => {
    const before = sampleProcesses();
    const startNanoseconds = nowNanoseconds();
    const value = await body();

    phases.push(measurePhase(started, { before, name, startNanoseconds }));

    return value;
  };

/** Cold start runs from spawn to the first usable state; the app idles, then runs its workload. */
const recordPhases = async (page: DevTools, started: Started): Promise<Omit<AppRun, "gpuInfo" | "trace">> => {
  const readyNanoseconds = await waitForThreadList(page, STARTUP_TIMEOUT_MS);
  const phases = [
    measurePhase(started, {
      before: started.atSpawn,
      name: "Cold start",
      startNanoseconds: started.spawnedNanoseconds,
    }),
  ];
  const measure = createMeasure(phases, started);

  await delay(SETTLE_MS);
  await measure("Idle", () => delay(SETTLE_MS));

  const workload = await driveWorkload({ ...started.run, measure, page });
  const ticksPerSecond = Number(execFileSync("getconf", ["CLK_TCK"], { encoding: "utf8" }));

  return { phases, readyNanoseconds, spawnedNanoseconds: started.spawnedNanoseconds, ticksPerSecond, workload };
};

const connectAndRecord = async (started: Started): Promise<Omit<AppRun, "trace">> => {
  const port = await readDevToolsPort(started.trace);
  const page = await connectToPage(port, APP_URL_PREFIX, STARTUP_TIMEOUT_MS);

  try {
    return { ...(await recordPhases(page, started)), gpuInfo: await readGpuInfo(port, STARTUP_TIMEOUT_MS) };
  } finally {
    page.close();
  }
};

/** The trace is read after the app has stopped, so it holds the presentation events of the last frames too. */
const runApp = async (run: ElectronRun): Promise<AppRun> => {
  const trace: Trace = { text: "" };
  const atSpawn = sampleProcesses();
  const spawnedNanoseconds = nowNanoseconds();
  const app = startApp(run);
  const record = (chunk: Buffer): void => {
    trace.text += chunk.toString();
  };

  app.stdout?.on("data", record);
  app.stderr?.on("data", record);

  const recorded = await connectAndRecord({ app, atSpawn, run, spawnedNanoseconds, trace }).finally(() =>
    stopProcessTree(app),
  );

  return { ...recorded, trace: trace.text };
};

export { runApp };
