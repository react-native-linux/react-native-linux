import { argv, env, stderr, stdout } from "node:process";
import {
  attachTrace,
  editAndAwaitRefresh,
  injectSteps,
  injectorBinaryPath,
  startCompositor,
  stopCompositor,
  waitForSocketName,
  windowBinaryPath,
  withDevServer,
} from "./e2e-processes.ts";
import { describeTraceFailures, resolveArtifactPaths, resolveExpectedOutcome } from "./e2e/scenario.ts";
import { existsSync, mkdirSync, mkdtempSync, readFileSync, readdirSync, rmSync, writeFileSync } from "node:fs";
import { findExecutable, findLavapipeIcd, resolveScenarioBundle } from "./window-golden.ts";
import { gradeArtifacts, gradeAutomationChannel, injectAndResolveFailure } from "./e2e/grade.ts";
import {
  isKeyboardFocused,
  planRuns,
  readRequestedScenarios,
  waitForWindowReadyFailures,
  waitUntil,
} from "./e2e/discovery.ts";
import path from "node:path";
import { tmpdir } from "node:os";

const FAILURE_EXIT_STATUS = 1;
const UNAVAILABLE_EXIT_STATUS = 2;
const EMPTY_LENGTH = 0;
const READY_TIMEOUT_MS = 60_000;
const RUN_TIMEOUT_MS = 120_000;

/** Cage: weston only offers weston-test, shipped nowhere. See *E2E driver (#7)* in docs/cpp-toolchain.md. */
const COMPOSITOR_NAME = "cage";

const repositoryRoot = path.resolve(import.meta.dirname, "..");
const packagesDirectory = path.join(repositoryRoot, "packages");
const artifactsRoot = path.join(repositoryRoot, "build", "e2e");

type ScenarioRun = ReturnType<typeof readRequestedScenarios>[number];
type Artifacts = ReturnType<typeof resolveArtifactPaths>;
type Compositor = ReturnType<typeof startCompositor>;

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

const driveScenario = async (run: ScenarioRun, workspace: Workspace): Promise<readonly string[]> => {
  const { scenario } = run;
  const socketName = await waitForSocketName(workspace.runtimeDirectory);
  // A missing socket short-circuits the ready wait; both bail-outs share the check below (#373).
  const readyFailures =
    socketName === null
      ? [`${COMPOSITOR_NAME} never created a wayland socket in ${workspace.runtimeDirectory}`]
      : await waitForWindowReadyFailures(
          { bundleReadyTraceLine: scenario.ready, expectedExitTraceLine: scenario.expectsExitAfter ?? null },
          workspace.trace,
          (isReady) => waitUntil(isReady, READY_TIMEOUT_MS),
        );

  if (socketName === null || readyFailures.length !== EMPTY_LENGTH) {
    return readyFailures;
  }

  const injectionFailure = await injectAndResolveFailure({
    inject: (steps) => injectSteps(steps, workspace.runtimeDirectory, socketName),
    scenario,
    waitForExpectedClose: () =>
      waitUntil(() => workspace.trace.text.includes(scenario.expectsExitAfter ?? ""), READY_TIMEOUT_MS),
    waitForKeyboardFocus: () => waitUntil(() => isKeyboardFocused(workspace.trace.text), READY_TIMEOUT_MS),
  });
  const refreshFailures = await editAndAwaitRefresh(scenario, workspace.trace);
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

  return [...(injectionFailure === null ? [] : [injectionFailure]), ...refreshFailures, ...automationFailures];
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

interface WindowRun {
  readonly failures: readonly string[];
  readonly signal: NodeJS.Signals | null;
}

interface WindowLaunch {
  readonly bundle: string;
  readonly rig: Rig;
  readonly run: ScenarioRun;
  readonly workspace: Workspace;
}

const runCompositor = async ({ bundle, rig, run, workspace }: WindowLaunch): Promise<WindowRun> => {
  const compositor = startCompositor({ ...workspace, bundle, rig, scenario: run.scenario });

  attachTrace(compositor, workspace.trace);

  const failures = await driveAndStop(run, compositor, workspace);

  return { failures, signal: compositor.signalCode };
};

/** A `fastRefresh` scenario's window runs its bundle from a watching Metro; see `withDevServer`. */
const runWindow = (run: ScenarioRun, rig: Rig, workspace: Workspace): Promise<WindowRun> => {
  const { fastRefresh } = run.scenario;
  const entryPath = path.join(run.source.bundlesDirectory, run.scenario.bundle);

  return fastRefresh === null
    ? runCompositor({ bundle: resolveScenarioBundle(run.source.bundlesDirectory, run.scenario), rig, run, workspace })
    : withDevServer(entryPath, fastRefresh, (bundle) => runCompositor({ bundle, rig, run, workspace }));
};

const runScenario = async (run: ScenarioRun, rig: Rig, attemptKey: string): Promise<readonly string[]> => {
  const artifacts = resolveArtifactPaths(artifactsRoot, attemptKey);
  const workspace = createWorkspace(artifacts);
  const { failures: runFailures, signal } = await runWindow(run, rig, workspace);

  collectArtifacts(artifacts.tracePath, workspace);

  const grade = gradeArtifacts({
    frameLogPath: artifacts.frameLogPath,
    goldensDirectory: run.source.goldensDirectory,
    scenario: run.scenario,
    screenshotPath: artifacts.screenshotPath,
  });

  stdout.write(grade.notes.map((note) => `e2e ${attemptKey}: ${note}\n`).join(""));

  const failures = [...runFailures, ...describeTraceFailures(run.scenario, workspace.trace.text), ...grade.failures];

  return resolveExpectedOutcome(run.scenario, failures, { signal, trace: workspace.trace.text });
};

const reportScenario = (failures: readonly string[], attemptKey: string): void => {
  if (failures.length === EMPTY_LENGTH) {
    stdout.write(`e2e ${attemptKey}: passed\n`);

    return;
  }

  const artifacts = resolveArtifactPaths(artifactsRoot, attemptKey);

  stderr.write(`e2e ${attemptKey}: failed\n${failures.join("\n")}\nartifacts: ${artifacts.directory}\n`);
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

  for (const { attemptKey, run } of planRuns(readScenarios(), env)) {
    reportScenario(await runScenario(run, { compositorPath, lavapipeIcdPath }, attemptKey), attemptKey);
  }
}
