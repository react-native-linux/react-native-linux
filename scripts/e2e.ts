import { argv, stderr, stdout } from "node:process";
import { buildEnvironment, findExecutable, findLavapipeIcd } from "./window-golden.ts";
import {
  describeTraceFailures,
  formatInjectorScript,
  resolveArtifactPaths,
  resolveExpectedOutcome,
} from "./e2e/scenario.ts";
import { existsSync, mkdirSync, mkdtempSync, readFileSync, readdirSync, rmSync, writeFileSync } from "node:fs";
import { gradeArtifacts, gradeAutomationChannel } from "./e2e/grade.ts";
import { spawn, spawnSync } from "node:child_process";

import { setTimeout as delay } from "node:timers/promises";
import path from "node:path";
import { readRequestedScenarios } from "./e2e/discovery.ts";
import { tmpdir } from "node:os";

const FAILURE_EXIT_STATUS = 1;
const UNAVAILABLE_EXIT_STATUS = 2;
const SUCCESSFUL_EXIT_STATUS = 0;
const EMPTY_LENGTH = 0;
const SOCKET_TIMEOUT_MS = 15_000;
const READY_TIMEOUT_MS = 60_000;
const RUN_TIMEOUT_MS = 120_000;
const INJECT_TIMEOUT_MS = 60_000;
const POLL_INTERVAL_MS = 50;
const COMPOSITOR_STOP_GRACE_MS = 250;

/**
 * The compositor is cage rather than the weston the window goldens use, because weston implements no virtual-input
 * protocol at all: its only injection surface is the `weston-test` plugin, which upstream builds with
 * `install: false` and no distribution ships. See *E2E driver (#7)* in docs/cpp-toolchain.md.
 */
const COMPOSITOR_NAME = "cage";
const SOCKET_PATTERN = /^wayland-\d+$/u;

const repositoryRoot = path.resolve(import.meta.dirname, "..");
const packagesDirectory = path.join(repositoryRoot, "packages");
const windowBinaryPath = path.join(repositoryRoot, "build", "dev", "bin", "rnl_window");
const injectorBinaryPath = path.join(repositoryRoot, "build", "dev", "bin", "rnl_inject");
const artifactsRoot = path.join(repositoryRoot, "build", "e2e");

type ScenarioRun = ReturnType<typeof readRequestedScenarios>[number];
type Scenario = ScenarioRun["scenario"];
type Artifacts = ReturnType<typeof resolveArtifactPaths>;
type Compositor = ReturnType<typeof spawn>;

interface TraceSink {
  /** Set once the compositor's stdio has closed, which is later than its exit. */
  isClosed: boolean;
  text: string;
}

interface Rig {
  readonly compositorPath: string;
  readonly lavapipeIcdPath: string;
}

interface Workspace {
  readonly artifactsDirectory: string;
  readonly frameLogPath: string;
  readonly runtimeDirectory: string;
  readonly screenshotPath: string;
  readonly trace: TraceSink;
}

const readScenarios = (): readonly ScenarioRun[] =>
  readRequestedScenarios(packagesDirectory, argv, {
    listEntries: (directory) => (existsSync(directory) ? readdirSync(directory) : []),
    readTextFile: (filePath) => readFileSync(filePath, "utf8"),
  });

const createWorkspace = (artifacts: Artifacts): Workspace => {
  mkdirSync(artifacts.directory, { recursive: true });

  return {
    artifactsDirectory: artifacts.directory,
    frameLogPath: artifacts.frameLogPath,
    runtimeDirectory: mkdtempSync(path.join(tmpdir(), "rnl-e2e-")),
    screenshotPath: artifacts.screenshotPath,
    trace: { isClosed: false, text: "" },
  };
};

/**
 * The headless wlroots backend needs no DRM device and no seat, and the pixman renderer needs no GPU at all: the
 * compositor only has to accept the client's buffers, because the screenshot comes out of the client's own
 * swapchain. cage runs the window as its child and terminates when it exits, so the two are one process tree.
 */
const startCompositor = (run: ScenarioRun, rig: Rig, workspace: Workspace): Compositor =>
  spawn(
    rig.compositorPath,
    [
      "--",
      windowBinaryPath,
      "--fabric",
      path.join(run.source.bundlesDirectory, run.scenario.bundle),
      "--frames",
      String(run.scenario.frames),
      "--screenshot",
      workspace.screenshotPath,
      "--frame-log",
      workspace.frameLogPath,
      ...(run.scenario.automation === null ? [] : ["--automation"]),
    ],
    {
      env: buildEnvironment({
        VK_ICD_FILENAMES: rig.lavapipeIcdPath,
        WLR_BACKENDS: "headless",
        WLR_LIBINPUT_NO_DEVICES: "1",
        WLR_RENDERER: "pixman",
        XDG_RUNTIME_DIR: workspace.runtimeDirectory,
        XKB_DEFAULT_LAYOUT: "us",
      }),
      stdio: ["ignore", "pipe", "pipe"],
    },
  );

/**
 * The event trace is the bundle's own `console.log` output, which the C++ console binding writes to the window's
 * stdout and cage passes through. The window has no trace format of its own; the fixtures are the format.
 */
const attachTrace = (compositor: Compositor, sink: TraceSink): void => {
  const record = (chunk: Buffer): void => {
    sink.text += chunk.toString();
  };

  compositor.stdout?.on("data", record);
  compositor.stderr?.on("data", record);
  compositor.on("close", () => {
    sink.isClosed = true;
  });
  compositor.on("error", (error: Error) => {
    sink.text += `${error.message}\n`;
  });
};

const waitUntil = async (isReady: () => boolean, timeoutMilliseconds: number): Promise<boolean> => {
  const deadline = Date.now() + timeoutMilliseconds;

  while (!isReady()) {
    if (Date.now() > deadline) {
      return false;
    }

    await delay(POLL_INTERVAL_MS);
  }

  return true;
};

const findSocketName = (runtimeDirectory: string): string | null =>
  readdirSync(runtimeDirectory).find((entry) => SOCKET_PATTERN.test(entry)) ?? null;

const waitForSocketName = async (runtimeDirectory: string): Promise<string | null> => {
  await waitUntil(() => findSocketName(runtimeDirectory) !== null, SOCKET_TIMEOUT_MS);

  return findSocketName(runtimeDirectory);
};

const stopCompositor = async (compositor: Compositor): Promise<void> => {
  compositor.kill("SIGTERM");
  await delay(COMPOSITOR_STOP_GRACE_MS);
  compositor.kill("SIGKILL");
};

const injectSteps = (scenario: Scenario, runtimeDirectory: string, socketName: string): string | null => {
  const injection = spawnSync(injectorBinaryPath, [], {
    encoding: "utf8",
    env: buildEnvironment({
      WAYLAND_DISPLAY: socketName,
      XDG_RUNTIME_DIR: runtimeDirectory,
      XKB_DEFAULT_LAYOUT: "us",
    }),
    input: formatInjectorScript(scenario.steps),
    timeout: INJECT_TIMEOUT_MS,
  });

  if (injection.status === SUCCESSFUL_EXIT_STATUS) {
    return null;
  }

  return `rnl_inject exited with status ${String(injection.status)}:\n${injection.stdout}${injection.stderr}`;
};

const driveScenario = async (run: ScenarioRun, workspace: Workspace): Promise<readonly string[]> => {
  const { scenario } = run;
  const socketName = await waitForSocketName(workspace.runtimeDirectory);

  if (socketName === null) {
    return [`${COMPOSITOR_NAME} never created a wayland socket in ${workspace.runtimeDirectory}`];
  }

  if (!(await waitUntil(() => workspace.trace.text.includes(scenario.ready), READY_TIMEOUT_MS))) {
    return [`the bundle never printed "${scenario.ready}"`];
  }

  const injectionFailure = injectSteps(scenario, workspace.runtimeDirectory, socketName);
  const automationFailures = await gradeAutomationChannel({
    artifactsDirectory: workspace.artifactsDirectory,
    goldensDirectory: run.source.goldensDirectory,
    scenario,
    snapshotsDirectory: run.source.snapshotsDirectory,
    trace: workspace.trace.text,
  });

  /*
   * The window exits on its own once it has captured the frame its budget names. The wait is for the streams to
   * close, not the exit code: a process can exit with its last lines still in flight, and #233's gate needs them.
   */
  await waitUntil(() => workspace.trace.isClosed, RUN_TIMEOUT_MS);

  return injectionFailure === null ? automationFailures : [injectionFailure, ...automationFailures];
};

const collectArtifacts = (tracePath: string, workspace: Workspace): void => {
  writeFileSync(tracePath, workspace.trace.text);
  rmSync(workspace.runtimeDirectory, { force: true, recursive: true });
};

const driveAndStop = async (
  run: ScenarioRun,
  compositor: Compositor,
  workspace: Workspace,
): Promise<readonly string[]> => {
  try {
    return await driveScenario(run, workspace);
  } finally {
    await stopCompositor(compositor);
  }
};

const runScenario = async (run: ScenarioRun, rig: Rig): Promise<readonly string[]> => {
  const artifacts = resolveArtifactPaths(artifactsRoot, run.scenario.name);
  const workspace = createWorkspace(artifacts);
  const compositor = startCompositor(run, rig, workspace);

  attachTrace(compositor, workspace.trace);

  const runFailures = await driveAndStop(run, compositor, workspace);

  collectArtifacts(artifacts.tracePath, workspace);

  const grade = gradeArtifacts({
    frameLogPath: artifacts.frameLogPath,
    goldensDirectory: run.source.goldensDirectory,
    scenario: run.scenario,
    screenshotPath: artifacts.screenshotPath,
  });

  stdout.write(grade.notes.map((note) => `e2e ${run.scenario.name}: ${note}\n`).join(""));

  const failures = [...runFailures, ...describeTraceFailures(run.scenario, workspace.trace.text), ...grade.failures];

  return resolveExpectedOutcome(run.scenario, failures);
};

const reportScenario = (scenario: Scenario, failures: readonly string[]): void => {
  if (failures.length === EMPTY_LENGTH) {
    stdout.write(`e2e ${scenario.name}: passed\n`);

    return;
  }

  const artifacts = resolveArtifactPaths(artifactsRoot, scenario.name);

  stderr.write(`e2e ${scenario.name}: failed\n${failures.join("\n")}\nartifacts: ${artifacts.directory}\n`);
  process.exitCode = FAILURE_EXIT_STATUS;
};

const compositorPath = findExecutable(COMPOSITOR_NAME);
const lavapipeIcdPath = findLavapipeIcd();

const unavailableReasons = [
  ...(existsSync(windowBinaryPath)
    ? []
    : [`${windowBinaryPath} is missing; build it with "cmake --build build/dev --target rnl_window"`]),
  ...(existsSync(injectorBinaryPath)
    ? []
    : [`${injectorBinaryPath} is missing; build it with "cmake --build build/dev --target rnl_inject"`]),
  ...(compositorPath === null ? [`${COMPOSITOR_NAME} is not on PATH; install the "cage" package`] : []),
  ...(lavapipeIcdPath === null
    ? ['no lavapipe ICD; install "vulkan-swrast" on Arch or "mesa-vulkan-drivers" on Ubuntu']
    : []),
];

if (compositorPath === null || lavapipeIcdPath === null || unavailableReasons.length !== EMPTY_LENGTH) {
  for (const reason of unavailableReasons) {
    stderr.write(`${reason}\n`);
  }

  process.exitCode = UNAVAILABLE_EXIT_STATUS;
} else {
  mkdirSync(artifactsRoot, { recursive: true });

  for (const run of readScenarios()) {
    reportScenario(run.scenario, await runScenario(run, { compositorPath, lavapipeIcdPath }));
  }
}
