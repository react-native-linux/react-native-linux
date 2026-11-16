import { describe, expect, it, vi } from "vitest";
import {
  injectAndResolveFailure,
  resolveCompositorChild,
  resolveInjectionFailure,
  resolveWindowFlags,
} from "./drive.ts";
import { parseScenario } from "./scenario.ts";

const EXPECTED_CLOSE_STATUS = 1;
const USAGE_ERROR_STATUS = 2;
const INTERRUPTED_STATUS = null;
const CLOSE_SUBSTRING = "the window closed before frame";
const OUTPUT_REFRESH_HZ = 120;

const waitForExpectedClose =
  (closed: boolean): (() => Promise<boolean>) =>
  () =>
    Promise.resolve(closed);

const POINTER_BOUND_TRACE_LINE = "[rnl-seat] pointer bound";

interface FakeTrace {
  readonly isClosed: () => boolean;
  readonly read: () => string;
  readonly waitFor: (isSatisfied: (text: string) => boolean) => Promise<boolean>;
}

/** A trace the window writes `lines` to once the driver first waits on it, as it binds the injector's pointer. */
const traceProducing = (lines: readonly string[]): FakeTrace => {
  let text = "";

  return {
    isClosed: () => false,
    read: () => text,
    waitFor: (isSatisfied) => {
      text = lines.join("\n");

      return Promise.resolve(isSatisfied(text));
    },
  };
};

const boundPointerTrace = (): FakeTrace => traceProducing([POINTER_BOUND_TRACE_LINE]);

const CLEAN_EXIT_STATUS = 0;

type StartInjector = Parameters<typeof injectAndResolveFailure>[number]["startInjector"];

const injectorExiting =
  (failure: string | null, status: number | null): StartInjector =>
  (): ReturnType<StartInjector> => ({
    environment: {},
    finish: () => Promise.resolve({ failure, status }),
    send: vi.fn(),
  });

const injectionScenario = (expectsExitAfter: string | null): ReturnType<typeof parseScenario> =>
  parseScenario(
    { bundle: "b.js", expect: ["ready"], expectsExitAfter, name: "s", ready: "ready", steps: ["sleep 1"] },
    "fixture.json",
  );

describe("resolveWindowFlags", () => {
  it("defaults an omitted field to --no-decorations", () => expect(resolveWindowFlags()).toEqual(["--no-decorations"]));

  it("keeps an explicit empty array as the compositor default", () => expect(resolveWindowFlags([])).toEqual([]));

  it("uses an explicit list exactly as written", () =>
    expect(resolveWindowFlags(["--force-client-decorations"])).toEqual(["--force-client-decorations"]));
});

describe("resolveCompositorChild", () => {
  it("runs the window directly at cage's own refresh rate", () =>
    expect(resolveCompositorChild(null, ["rnl_window", "--frames", "1"])).toEqual(["rnl_window", "--frames", "1"]));

  it("sets the headless output's custom mode before it runs the window", () =>
    expect(resolveCompositorChild(OUTPUT_REFRESH_HZ, ["rnl_window", "--frames", "1"])).toEqual([
      "sh",
      "-c",
      'wlr-randr --output HEADLESS-1 --off && wlr-randr --output HEADLESS-1 --on --custom-mode 1280x720@120Hz && exec "$0" "$@"',
      "rnl_window",
      "--frames",
      "1",
    ]));
});

describe("resolveInjectionFailure", () => {
  it("passes a null failure straight through", async () => {
    const outcome = { failure: null, status: EXPECTED_CLOSE_STATUS };

    expect(await resolveInjectionFailure(outcome, CLOSE_SUBSTRING, waitForExpectedClose(true))).toBeNull();
  });

  it("keeps a failure the scenario never opted into forgiving", async () => {
    const outcome = { failure: "boom", status: EXPECTED_CLOSE_STATUS };

    expect(await resolveInjectionFailure(outcome, null, waitForExpectedClose(true))).toBe("boom");
  });

  it("forgives status 1 once the expected substring is observed", async () => {
    const outcome = { failure: "boom", status: EXPECTED_CLOSE_STATUS };

    expect(await resolveInjectionFailure(outcome, CLOSE_SUBSTRING, waitForExpectedClose(true))).toBeNull();
  });

  it("keeps the failure when the expected substring never shows up", async () => {
    const outcome = { failure: "boom", status: EXPECTED_CLOSE_STATUS };

    expect(await resolveInjectionFailure(outcome, CLOSE_SUBSTRING, waitForExpectedClose(false))).toBe("boom");
  });

  it("keeps a status 2 failure even with a willing waiter, since 2 is not the socket disappearing", async () => {
    const outcome = { failure: "boom", status: USAGE_ERROR_STATUS };

    expect(await resolveInjectionFailure(outcome, CLOSE_SUBSTRING, waitForExpectedClose(true))).toBe("boom");
  });

  it("keeps a signal-interrupted (status null) failure even with a willing waiter", async () => {
    const outcome = { failure: "boom", status: INTERRUPTED_STATUS };

    expect(await resolveInjectionFailure(outcome, CLOSE_SUBSTRING, waitForExpectedClose(true))).toBe("boom");
  });
});

describe("injectAndResolveFailure", () => {
  it("passes a successful injection through as null", async () => {
    const result = await injectAndResolveFailure({
      scenario: injectionScenario(null),
      startInjector: injectorExiting(null, EXPECTED_CLOSE_STATUS),
      trace: boundPointerTrace(),
      waitForExpectedClose: waitForExpectedClose(true),
      waitForKeyboardFocus: waitForExpectedClose(true),
    });

    expect(result).toBeNull();
  });

  it("forgives status 1 for an expectsExitAfter scenario once the substring traces", async () => {
    const result = await injectAndResolveFailure({
      scenario: injectionScenario(CLOSE_SUBSTRING),
      startInjector: injectorExiting("boom", EXPECTED_CLOSE_STATUS),
      trace: boundPointerTrace(),
      waitForExpectedClose: waitForExpectedClose(true),
      waitForKeyboardFocus: waitForExpectedClose(true),
    });

    expect(result).toBeNull();
  });

  it("keeps a status 2 failure even for an expectsExitAfter scenario", async () => {
    const result = await injectAndResolveFailure({
      scenario: injectionScenario(CLOSE_SUBSTRING),
      startInjector: injectorExiting("boom", USAGE_ERROR_STATUS),
      trace: boundPointerTrace(),
      waitForExpectedClose: waitForExpectedClose(true),
      waitForKeyboardFocus: waitForExpectedClose(true),
    });

    expect(result).toBe("boom");
  });
});

const awaitScenario = parseScenario(
  { bundle: "b.js", expect: ["ready"], name: "s", ready: "ready", steps: ["pan 1 0", "await settled"] },
  "fixture.json",
);

describe("injectAndResolveFailure with an await step", () => {
  it("reports an await that never traced once the injector exits cleanly", async () => {
    const result = await injectAndResolveFailure({
      scenario: awaitScenario,
      startInjector: injectorExiting(null, CLEAN_EXIT_STATUS),
      trace: boundPointerTrace(),
      waitForExpectedClose: waitForExpectedClose(true),
      waitForKeyboardFocus: waitForExpectedClose(true),
    });

    expect(result).toBe('the trace never produced "settled" before the steps after "await settled"');
  });

  it("reports the await rather than the injector killed while it waited", async () => {
    const result = await injectAndResolveFailure({
      scenario: awaitScenario,
      startInjector: injectorExiting("rnl_inject exited with status null:\n", null),
      trace: boundPointerTrace(),
      waitForExpectedClose: waitForExpectedClose(true),
      waitForKeyboardFocus: waitForExpectedClose(true),
    });

    expect(result).toBe('the trace never produced "settled" before the steps after "await settled"');
  });
});

/** An injector that exits cleanly and records every line the driver sends it, in order. */
const recordingInjector = (sent: string[]): ReturnType<StartInjector> => ({
  environment: {},
  finish: () => Promise.resolve({ failure: null, status: CLEAN_EXIT_STATUS }),
  send: (steps) => {
    sent.push(...steps);
  },
});

describe("injectAndResolveFailure before the window binds the pointer", () => {
  it("holds every step back until the window traces the pointer it bound after the injector started", async () => {
    const sent: string[] = [];
    const result = await injectAndResolveFailure({
      scenario: injectionScenario(null),
      startInjector: () => recordingInjector(sent),
      trace: traceProducing([]),
      waitForExpectedClose: waitForExpectedClose(true),
      waitForKeyboardFocus: waitForExpectedClose(true),
    });

    expect(result).toBe(
      `the trace never produced "${POINTER_BOUND_TRACE_LINE}" before the steps after "await ${POINTER_BOUND_TRACE_LINE}"`,
    );
    expect(sent).toEqual([]);
  });

  it("sends the steps on both sides of the keyboard split to the one injector", async () => {
    const sent: string[] = [];
    const startInjector = vi.fn((): ReturnType<StartInjector> => recordingInjector(sent));
    const result = await injectAndResolveFailure({
      scenario: parseScenario(
        { bundle: "b.js", expect: ["ready"], name: "s", ready: "ready", steps: ["click 1 1", "key Tab press"] },
        "fixture.json",
      ),
      startInjector,
      trace: boundPointerTrace(),
      waitForExpectedClose: waitForExpectedClose(true),
      waitForKeyboardFocus: waitForExpectedClose(true),
    });

    expect(result).toBeNull();
    expect(startInjector).toHaveBeenCalledOnce();
    expect(sent).toEqual(["click 1 1", "key Tab press"]);
  });
});
