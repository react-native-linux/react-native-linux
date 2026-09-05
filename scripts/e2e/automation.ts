import { existsSync, readFileSync, writeFileSync } from "node:fs";

import type { ScenarioAutomation } from "./scenario.ts";
import { connect } from "node:net";
import { once } from "node:events";
import path from "node:path";

const FIRST_CHARACTER = 0;
const LINE_TERMINATOR = "\n";
const COMMAND_TIMEOUT_MS = 10_000;
const HANG_MS = 1500;
const HANG_TIMEOUT_MS = 300;
const NO_ERRORS = 0;
const JSON_INDENT = 2;

const TREE_ARTIFACT_NAME = "automation-tree.json";
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
  readonly trace: string;
}

const isRecord = (value: unknown): value is Record<string, unknown> =>
  typeof value === "object" && value !== null && !Array.isArray(value);

const findAutomationSocketPath = (trace: string): string | null =>
  SOCKET_TRACE_PATTERN.exec(trace)?.groups?.["socketPath"] ?? null;

const readAnswer = (line: string): AutomationAnswer => {
  let response: unknown = null;

  try {
    response = JSON.parse(line);
  } catch {
    return { failure: `the window answered with a line that is not JSON: ${line}`, result: null };
  }

  if (!isRecord(response)) {
    return { failure: `the window answered with a line that is not a JSON object: ${line}`, result: null };
  }

  if (response["ok"] !== true) {
    return { failure: typeof response["error"] === "string" ? response["error"] : line, result: null };
  }

  const { result } = response;

  return isRecord(result)
    ? { failure: null, result }
    : { failure: `the window answered without a result object: ${line}`, result: null };
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
 * Sends one request and waits for the one line that answers it, on a connection of its own. A connection per
 * command rather than one kept open for the run: the window serves one client at a time and one request per
 * frame, so a driver holding the socket across a `HangForTesting` would deadlock its next command against the
 * hang it just asked for.
 *
 * Every way this can go wrong is one answer with a `failure`: no socket, a refused connection, a malformed line,
 * and — the case `HangForTesting` exists to produce — a window that never answers before the deadline.
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
    };
  } finally {
    socket.destroy();
  }
};

/**
 * Key-sorted JSON, so a snapshot compares by structure rather than by the order `folly::dynamic` happened to
 * hash its object keys into.
 */
const canonicalJson = (value: unknown): string => {
  if (Array.isArray(value)) {
    return `[${value.map((entry) => canonicalJson(entry)).join(",")}]`;
  }

  if (!isRecord(value)) {
    return JSON.stringify(value) ?? "null";
  }

  return `{${Object.keys(value)
    .toSorted()
    .map((key) => `${JSON.stringify(key)}:${canonicalJson(value[key])}`)
    .join(",")}}`;
};

/**
 * What a snapshot is compared against: the children of the surface root, not the roots themselves. The surface
 * root's frame is the headless compositor's output size, which is a property of the rig rather than of the
 * bundle, so a snapshot carrying it would have to be re-blessed whenever the rig changed size.
 */
const findSurfaceChildren = (result: Record<string, unknown>): unknown => {
  const { roots } = result;
  const surfaceRoot: unknown = Array.isArray(roots) ? roots[FIRST_CHARACTER] : null;

  return isRecord(surfaceRoot) ? (surfaceRoot["children"] ?? []) : null;
};

const describeReportedErrors = (result: Record<string, unknown>): readonly string[] => {
  const { errors } = result;

  if (!Array.isArray(errors) || errors.length === NO_ERRORS) {
    return [];
  }

  return [`ListErrors reported ${String(errors.length)} error(s): ${JSON.stringify(errors)}`];
};

const compareVisualTree = (
  inputs: AutomationInputs,
  snapshot: string,
  result: Record<string, unknown>,
): readonly string[] => {
  const observedPath = path.join(inputs.artifactsDirectory, TREE_ARTIFACT_NAME);
  const observed = findSurfaceChildren(result);

  writeFileSync(observedPath, `${JSON.stringify(observed, null, JSON_INDENT)}\n`);

  const snapshotPath = path.join(inputs.goldensDirectory, snapshot);

  if (!existsSync(snapshotPath)) {
    return [`there is no visual-tree snapshot at ${snapshotPath}; bless ${observedPath}`];
  }

  const expected: unknown = JSON.parse(readFileSync(snapshotPath, "utf8"));

  return canonicalJson(observed) === canonicalJson(expected)
    ? []
    : [`the visual tree does not match ${snapshot}; see ${observedPath}`];
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

  return answer.result === null ? [String(answer.failure)] : compareVisualTree(inputs, snapshot, answer.result);
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

const describeScreenshotFailure = (answer: AutomationAnswer, screenshotPath: string): readonly string[] => {
  if (answer.result === null) {
    return [String(answer.failure)];
  }

  return existsSync(screenshotPath) ? [] : [`TakeScreenshot answered but wrote no picture at ${screenshotPath}`];
};

/**
 * `TakeScreenshot` and `HangForTesting` are proved on every scenario that opens the channel rather than by a flag
 * of their own: neither asserts anything about the app, they assert that the channel itself reaches the renderer
 * and that a wedged JavaScript thread is observable as a timeout instead of as a wrong answer.
 */
const gradeChannelItself = async (socketPath: string, inputs: AutomationInputs): Promise<readonly string[]> => {
  const screenshotPath = path.join(inputs.artifactsDirectory, SCREENSHOT_ARTIFACT_NAME);
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

  return hang.result === null
    ? shotFailures
    : [...shotFailures, `HangForTesting answered inside ${String(HANG_TIMEOUT_MS)}ms, so it never blocked`];
};

/**
 * Drives the channel while the window is still up, in the order the commands cost: the assertions first, then
 * `TakeScreenshot`, then the hang, which leaves the JavaScript thread blocked and so has to be last.
 */
const gradeAutomation = async (inputs: AutomationInputs): Promise<readonly string[]> => {
  const socketPath = findAutomationSocketPath(inputs.trace);

  if (socketPath === null) {
    return ["the window never printed the automation socket path"];
  }

  return [
    ...(await gradeListErrors(socketPath, inputs)),
    ...(await gradeVisualTree(socketPath, inputs)),
    ...(await gradeMarkTestPassed(socketPath, inputs)),
    ...(await gradeChannelItself(socketPath, inputs)),
  ];
};

export { canonicalJson, findAutomationSocketPath, findSurfaceChildren, gradeAutomation, readAnswer, requestAutomation };
