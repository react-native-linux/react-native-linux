import { setTimeout as delay } from "node:timers/promises";
import { once } from "node:events";

/*
 * Just enough of the Chrome DevTools protocol to drive an Electron window from scripts/perf-electron.ts: one socket,
 * request ids matched to responses, and `Runtime.evaluate` by value.
 */

const POLL_INTERVAL_MS = 50;
const FIRST_REQUEST_ID = 1;
const REQUEST_ID_STEP = 1;

interface DevTools {
  readonly close: () => void;
  /** The expression's value, awaited if it is a promise and returned by value. */
  readonly evaluate: (expression: string) => Promise<unknown>;
  readonly send: (method: string, parameters?: Record<string, unknown>) => Promise<unknown>;
}

const isRecord = (value: unknown): value is Record<string, unknown> =>
  typeof value === "object" && value !== null && !Array.isArray(value);

const readResult = (event: unknown, method: string): unknown => {
  const message: unknown = event instanceof MessageEvent ? event.data : null;

  if (!isRecord(message) || isRecord(message["error"])) {
    throw new Error(`${method} failed: ${JSON.stringify(message)}`);
  }

  return message["result"];
};

const readEvaluatedValue = (result: unknown): unknown =>
  isRecord(result) && isRecord(result["result"]) ? result["result"]["value"] : null;

/** Every wait is bounded by `timeoutMilliseconds`, so a crashed window fails the run instead of hanging it. */
const connectDevTools = async (url: string, timeoutMilliseconds: number): Promise<DevTools> => {
  const socket = new WebSocket(url);
  const responses = new EventTarget();
  let nextId = FIRST_REQUEST_ID;
  const send: DevTools["send"] = async (method, parameters = {}) => {
    const id = nextId;
    nextId += REQUEST_ID_STEP;
    const response = once(responses, String(id), { signal: AbortSignal.timeout(timeoutMilliseconds) });

    socket.send(JSON.stringify({ id, method, params: parameters }));

    const [event]: unknown[] = await response;

    return readResult(event, method);
  };

  socket.addEventListener("message", (event) => {
    const message: unknown = JSON.parse(String(event.data));

    if (isRecord(message) && typeof message["id"] === "number") {
      responses.dispatchEvent(new MessageEvent(String(message["id"]), { data: message }));
    }
  });
  await once(socket, "open", { signal: AbortSignal.timeout(timeoutMilliseconds) });

  return {
    close: () => {
      socket.close();
    },
    evaluate: async (expression) =>
      readEvaluatedValue(await send("Runtime.evaluate", { awaitPromise: true, expression, returnByValue: true })),
    send,
  };
};

/** Calls `read` until it gives a value or the time is up, which gives null. */
const pollUntil = async <Value>(
  read: () => Promise<Value | null>,
  timeoutMilliseconds: number,
): Promise<Value | null> => {
  const deadline = Date.now() + timeoutMilliseconds;

  while (Date.now() < deadline) {
    const value = await read();

    if (value !== null) {
      return value;
    }
    await delay(POLL_INTERVAL_MS);
  }

  return null;
};

const readPageTargetUrl = async (port: string, urlPrefix: string): Promise<string | null> => {
  const response = await fetch(`http://127.0.0.1:${port}/json/list`);
  const targets: unknown = await response.json();
  const page = (Array.isArray(targets) ? targets : []).find(
    (target) => isRecord(target) && target["type"] === "page" && String(target["url"]).startsWith(urlPrefix),
  );

  return isRecord(page) && typeof page["webSocketDebuggerUrl"] === "string" ? page["webSocketDebuggerUrl"] : null;
};

/** The page whose URL starts with `urlPrefix`; Electron lists it once the window has loaded its first document. */
const connectToPage = async (port: string, urlPrefix: string, timeoutMilliseconds: number): Promise<DevTools> => {
  const url = await pollUntil(() => readPageTargetUrl(port, urlPrefix), timeoutMilliseconds);

  if (url === null) {
    throw new Error(`no ${urlPrefix} page appeared on the DevTools endpoint at port ${port}`);
  }

  return connectDevTools(url, timeoutMilliseconds);
};

export { connectToPage, isRecord, pollUntil };
