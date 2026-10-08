import { stdout } from "node:process";

const COMMAND_TIMEOUT_MS = 10_000;

interface RendererAnswer {
  readonly failure: string | null;
  readonly result: Record<string, unknown> | null;
}

type RequestRenderer = (
  socketPath: string,
  request: { readonly command: string },
  timeoutMilliseconds: number,
) => Promise<RendererAnswer>;

/**
 * The automation half of #368: the window has to name the renderer ladder rung it drew on, and the scenario fails
 * unless that is the rung it expected. The reason the window gives is printed rather than compared, because it
 * quotes whatever the failed rungs said and that is the driver's wording, not the scenario's.
 */
const gradeRendererRung = async (
  requestAutomation: RequestRenderer,
  socketPath: string,
  expectedRung: string | null,
): Promise<readonly string[]> => {
  if (expectedRung === null) {
    return [];
  }

  const answer = await requestAutomation(socketPath, { command: "DescribeRenderer" }, COMMAND_TIMEOUT_MS);

  if (answer.result === null) {
    return [String(answer.failure)];
  }

  const { reason, rung } = answer.result;

  stdout.write(`e2e DescribeRenderer: ${JSON.stringify(rung)} because ${JSON.stringify(reason)}\n`);

  return rung === expectedRung
    ? []
    : [`DescribeRenderer answered rung ${JSON.stringify(rung)}, expected ${JSON.stringify(expectedRung)}`];
};

export { gradeRendererRung };
