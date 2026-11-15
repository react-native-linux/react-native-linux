import { describe, expect, it } from "vitest";
import {
  injectAndResolveFailure,
  resolveCompositorChild,
  resolveInjectionFailure,
  resolveWindowFlags,
} from "./grade.ts";
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
      inject: () => ({ failure: null, status: EXPECTED_CLOSE_STATUS }),
      scenario: injectionScenario(null),
      waitForExpectedClose: waitForExpectedClose(true),
      waitForKeyboardFocus: waitForExpectedClose(true),
    });

    expect(result).toBeNull();
  });

  it("forgives status 1 for an expectsExitAfter scenario once the substring traces", async () => {
    const result = await injectAndResolveFailure({
      inject: () => ({ failure: "boom", status: EXPECTED_CLOSE_STATUS }),
      scenario: injectionScenario(CLOSE_SUBSTRING),
      waitForExpectedClose: waitForExpectedClose(true),
      waitForKeyboardFocus: waitForExpectedClose(true),
    });

    expect(result).toBeNull();
  });

  it("keeps a status 2 failure even for an expectsExitAfter scenario", async () => {
    const result = await injectAndResolveFailure({
      inject: () => ({ failure: "boom", status: USAGE_ERROR_STATUS }),
      scenario: injectionScenario(CLOSE_SUBSTRING),
      waitForExpectedClose: waitForExpectedClose(true),
      waitForKeyboardFocus: waitForExpectedClose(true),
    });

    expect(result).toBe("boom");
  });
});
