import { existsSync, rmSync } from "node:fs";

import type { ScenarioAutomation } from "./scenario.ts";
import { compareSnapshot } from "./snapshot.ts";
import { connect } from "node:net";
import { isRecord } from "./fields.ts";
import { once } from "node:events";
import path from "node:path";

const FIRST_CHARACTER = 0;
const LINE_TERMINATOR = "\n";
const COMMAND_TIMEOUT_MS = 10_000;
const HANG_MS = 1500;
const HANG_TIMEOUT_MS = 300;
const NO_ERRORS = 0;

const TREE_ARTIFACT_NAME = "automation-tree.json";
const ACCESSIBILITY_ARTIFACT_NAME = "accessibility-tree.json";
const SCREENSHOT_ARTIFACT_NAME = "automation-screenshot.png";

/**
 * The socket path the window prints to the trace when `--automation` opens the channel. The driver learns it
 * from the trace rather than computing it, because the name carries the window's process id and the driver only
 * knows the compositor's. See *The automation channel (#214)* in docs/cpp-toolchain.md.
 */
const SOCKET_TRACE_PATTERN = /\[rnl-automation\] listening on (?<socketPath>\S+)/u;

/**
 * What one command answered: the parsed `result` object, or the reason there is none. `failure` covers both the
 * window refusing the request — an unknown command, a missing argument — and the socket never answering, which
 * is what `HangForTesting` produces and what proves the timeout path.
 */
interface AutomationAnswer {
  readonly failure: string | null;
  readonly result: Record<string, unknown> | null;
  /** True only when the deadline passed, which is the one outcome `HangForTesting` is allowed to produce. */
  readonly timedOut: boolean;
}

interface AutomationRequest {
  readonly command: string;
  readonly milliseconds?: number;
  readonly path?: string;
}

interface AutomationInputs {
  readonly artifactsDirectory: string;
  readonly automation: ScenarioAutomation;
  readonly goldensDirectory: string;
  readonly snapshotsDirectory: string;
  readonly trace: string;
}

const findAutomationSocketPath = (trace: string): string | null =>
  SOCKET_TRACE_PATTERN.exec(trace)?.groups?.["socketPath"] ?? null;

const refuse = (failure: string): AutomationAnswer => ({ failure, result: null, timedOut: false });

const readAnswer = (line: string): AutomationAnswer => {
  let response: unknown = null;

  try {
    response = JSON.parse(line);
  } catch {
    return refuse(`the window answered with a line that is not JSON: ${line}`);
  }

  if (!isRecord(response)) {
    return refuse(`the window answered with a line that is not a JSON object: ${line}`);
  }

  if (response["ok"] !== true) {
    return refuse(typeof response["error"] === "string" ? response["error"] : line);
  }

  const { result } = response;

  return isRecord(result)
    ? { failure: null, result, timedOut: false }
    : refuse(`the window answered without a result object: ${line}`);
};

/** Reads until the newline that ends one response, and gives back the line without it. */
const readAnswerLine = async (socket: ReturnType<typeof connect>, deadline: AbortSignal): Promise<string> => {
  let received = "";

  while (!received.includes(LINE_TERMINATOR)) {
    const chunks: unknown[] = await once(socket, "data", { signal: deadline });

    received += String(chunks[FIRST_CHARACTER]);
  }

  return received.slice(FIRST_CHARACTER, received.indexOf(LINE_TERMINATOR));
};

/**
 * Sends one request and waits for the one line that answers it, on a connection of its own: the window serves
 * one client at a time and one request per frame, so a driver holding the socket across a `HangForTesting`
 * would deadlock its next command against the hang it just asked for.
 *
 * Every way this can go wrong is one answer with a `failure` — no socket, a refusal, a malformed line, a
 * deadline that passed — and only the last of those sets `timedOut`.
 */
const requestAutomation = async (
  socketPath: string,
  request: AutomationRequest,
  timeoutMilliseconds: number,
): Promise<AutomationAnswer> => {
  const socket = connect(socketPath);
  const deadline = AbortSignal.timeout(timeoutMilliseconds);

  try {
    await once(socket, "connect", { signal: deadline });
    socket.write(`${JSON.stringify(request)}${LINE_TERMINATOR}`);

    return readAnswer(await readAnswerLine(socket, deadline));
  } catch (error) {
    return {
      failure: `${request.command} did not answer within ${String(timeoutMilliseconds)}ms: ${String(error)}`,
      result: null,
      timedOut: deadline.aborted,
    };
  } finally {
    socket.destroy();
  }
};

/**
 * What a visual-tree snapshot is compared against: the children of the surface root, not the roots themselves.
 * The surface root's frame is the headless compositor's output size, which is a property of the rig rather than
 * of the bundle, so a snapshot carrying it would have to be re-blessed whenever the rig changed size.
 */
const findSurfaceChildren = (result: Record<string, unknown>): unknown => {
  const { roots } = result;
  const surfaceRoot: unknown = Array.isArray(roots) ? roots[FIRST_CHARACTER] : null;

  return isRecord(surfaceRoot) ? (surfaceRoot["children"] ?? []) : null;
};

/**
 * The accessibility projection needs no such trimming: it carries no geometry at all, and the surface root is a
 * box with no accessibility props, so the projection already skips it and answers with its exposed descendants.
 */
const findAccessibilityNodes = (result: Record<string, unknown>): unknown => {
  const { nodes } = result;

  return Array.isArray(nodes) ? nodes : null;
};

const describeReportedErrors = (result: Record<string, unknown>): readonly string[] => {
  const { errors } = result;

  if (!Array.isArray(errors) || errors.length === NO_ERRORS) {
    return [];
  }

  return [`ListErrors reported ${String(errors.length)} error(s): ${JSON.stringify(errors)}`];
};

const gradeListErrors = async (socketPath: string, inputs: AutomationInputs): Promise<readonly string[]> => {
  if (!inputs.automation.listErrorsMustBeEmpty) {
    return [];
  }

  const answer = await requestAutomation(socketPath, { command: "ListErrors" }, COMMAND_TIMEOUT_MS);

  return answer.result === null ? [String(answer.failure)] : describeReportedErrors(answer.result);
};

const gradeVisualTree = async (socketPath: string, inputs: AutomationInputs): Promise<readonly string[]> => {
  const snapshot = inputs.automation.visualTreeSnapshot;

  if (snapshot === null) {
    return [];
  }

  const answer = await requestAutomation(socketPath, { command: "DumpVisualTree" }, COMMAND_TIMEOUT_MS);

  if (answer.result === null) {
    return [String(answer.failure)];
  }

  return compareSnapshot(inputs.artifactsDirectory, {
    artifactName: TREE_ARTIFACT_NAME,
    directory: inputs.goldensDirectory,
    observed: findSurfaceChildren(answer.result),
    snapshot,
  });
};

/**
 * The assertion of #216: what a screen reader would be handed, compared against a file a reviewer reads in the
 * diff. It catches what a pixel diff cannot — a role, a name, a state, or a node that silently stopped being
 * exposed at all changes no pixel that a perceptual threshold would notice.
 */
const gradeAccessibilityTree = async (socketPath: string, inputs: AutomationInputs): Promise<readonly string[]> => {
  const snapshot = inputs.automation.accessibilityTreeSnapshot;

  if (snapshot === null) {
    return [];
  }

  const answer = await requestAutomation(socketPath, { command: "DumpAccessibilityTree" }, COMMAND_TIMEOUT_MS);

  if (answer.result === null) {
    return [String(answer.failure)];
  }

  return compareSnapshot(inputs.artifactsDirectory, {
    artifactName: ACCESSIBILITY_ARTIFACT_NAME,
    directory: inputs.snapshotsDirectory,
    observed: findAccessibilityNodes(answer.result),
    snapshot,
  });
};

const gradeMarkTestPassed = async (socketPath: string, inputs: AutomationInputs): Promise<readonly string[]> => {
  if (!inputs.automation.markTestPassed) {
    return [];
  }

  const answer = await requestAutomation(socketPath, { command: "MarkTestPassed" }, COMMAND_TIMEOUT_MS);

  if (answer.result === null) {
    return [String(answer.failure)];
  }

  return answer.result["passed"] === true ? [] : ["the bundle never called globalThis.__rnlMarkTestPassed()"];
};

/**
 * An answer means the JavaScript thread never blocked; any other failure means the command did not run at all,
 * which is not the same thing and must not pass as a hang.
 */
const describeHangFailure = (answer: AutomationAnswer): string =>
  answer.failure === null
    ? `HangForTesting answered inside ${String(HANG_TIMEOUT_MS)}ms, so it never blocked`
    : `HangForTesting failed instead of blocking: ${String(answer.failure)}`;

const describeScreenshotFailure = (answer: AutomationAnswer, screenshotPath: string): readonly string[] => {
  if (answer.result === null) {
    return [String(answer.failure)];
  }

  return existsSync(screenshotPath) ? [] : [`TakeScreenshot answered but wrote no picture at ${screenshotPath}`];
};

/**
 * `TakeScreenshot` and `HangForTesting` need no flag of their own: neither asserts anything about the app, they
 * assert that the channel reaches the renderer and that a wedged JavaScript thread reads as a timeout.
 */
const gradeChannelItself = async (socketPath: string, inputs: AutomationInputs): Promise<readonly string[]> => {
  const screenshotPath = path.join(inputs.artifactsDirectory, SCREENSHOT_ARTIFACT_NAME);

  rmSync(screenshotPath, { force: true });

  const shot = await requestAutomation(
    socketPath,
    { command: "TakeScreenshot", path: screenshotPath },
    COMMAND_TIMEOUT_MS,
  );
  const shotFailures = describeScreenshotFailure(shot, screenshotPath);
  const hang = await requestAutomation(
    socketPath,
    { command: "HangForTesting", milliseconds: HANG_MS },
    HANG_TIMEOUT_MS,
  );

  return hang.timedOut ? shotFailures : [...shotFailures, describeHangFailure(hang)];
};

/** In the order the commands cost: the assertions, then `TakeScreenshot`, then the hang, which has to be last. */
const gradeAutomation = async (inputs: AutomationInputs): Promise<readonly string[]> => {
  const socketPath = findAutomationSocketPath(inputs.trace);

  if (socketPath === null) {
    return ["the window never printed the automation socket path"];
  }

  return [
    ...(await gradeListErrors(socketPath, inputs)),
    ...(await gradeVisualTree(socketPath, inputs)),
    ...(await gradeAccessibilityTree(socketPath, inputs)),
    ...(await gradeMarkTestPassed(socketPath, inputs)),
    ...(await gradeChannelItself(socketPath, inputs)),
  ];
};

export {
  findAccessibilityNodes,
  findAutomationSocketPath,
  findSurfaceChildren,
  gradeAutomation,
  readAnswer,
  requestAutomation,
};
