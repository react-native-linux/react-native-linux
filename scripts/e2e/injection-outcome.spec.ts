import { describe, expect, it } from "vitest";
import { injectAndResolveFailure, resolveInjectionFailure, resolveWindowFlags } from "./grade.ts";
import { parseScenario } from "./scenario.ts";

const EXPECTED_CLOSE_STATUS = 1;
const USAGE_ERROR_STATUS = 2;
const INTERRUPTED_STATUS = null;

const waitForExpectedClose =
  (closed: boolean): (() => Promise<boolean>) =>
  () =>
    Promise.resolve(closed);

const injectionScenario = (expectsWindowClose: boolean): ReturnType<typeof parseScenario> =>
  parseScenario(
    { bundle: "b.js", expect: ["ready"], expectsWindowClose, name: "s", ready: "ready", steps: ["sleep 1"] },
    "fixture.json",
  );

describe("resolveWindowFlags", () => {
  it("defaults an omitted field to --no-decorations", () => expect(resolveWindowFlags()).toEqual(["--no-decorations"]));

  it("keeps an explicit empty array as the compositor default", () => expect(resolveWindowFlags([])).toEqual([]));

  it("uses an explicit list exactly as written", () =>
    expect(resolveWindowFlags(["--force-client-decorations"])).toEqual(["--force-client-decorations"]));
});

describe("resolveInjectionFailure", () => {
  it("passes a null failure straight through", async () => {
    const outcome = { failure: null, status: EXPECTED_CLOSE_STATUS };

    expect(await resolveInjectionFailure(outcome, true, waitForExpectedClose(true))).toBeNull();
  });

  it("keeps a failure the scenario never opted into forgiving", async () => {
    const outcome = { failure: "boom", status: EXPECTED_CLOSE_STATUS };

    expect(await resolveInjectionFailure(outcome, false, waitForExpectedClose(true))).toBe("boom");
  });

  it("forgives status 1 once the expected close is observed", async () => {
    const outcome = { failure: "boom", status: EXPECTED_CLOSE_STATUS };

    expect(await resolveInjectionFailure(outcome, true, waitForExpectedClose(true))).toBeNull();
  });

  it("keeps the failure when the expected close never shows up", async () => {
    const outcome = { failure: "boom", status: EXPECTED_CLOSE_STATUS };

    expect(await resolveInjectionFailure(outcome, true, waitForExpectedClose(false))).toBe("boom");
  });

  it("keeps a status 2 failure even with a willing waiter, since 2 is not the socket disappearing", async () => {
    const outcome = { failure: "boom", status: USAGE_ERROR_STATUS };

    expect(await resolveInjectionFailure(outcome, true, waitForExpectedClose(true))).toBe("boom");
  });

  it("keeps a signal-interrupted (status null) failure even with a willing waiter", async () => {
    const outcome = { failure: "boom", status: INTERRUPTED_STATUS };

    expect(await resolveInjectionFailure(outcome, true, waitForExpectedClose(true))).toBe("boom");
  });
});

describe("injectAndResolveFailure", () => {
  it("passes a successful injection through as null", async () => {
    const result = await injectAndResolveFailure({
      inject: () => ({ failure: null, status: EXPECTED_CLOSE_STATUS }),
      scenario: injectionScenario(false),
      waitForExpectedClose: waitForExpectedClose(true),
      waitForKeyboardFocus: waitForExpectedClose(true),
    });

    expect(result).toBeNull();
  });

  it("forgives status 1 for an expectsWindowClose scenario once the close line traces", async () => {
    const result = await injectAndResolveFailure({
      inject: () => ({ failure: "boom", status: EXPECTED_CLOSE_STATUS }),
      scenario: injectionScenario(true),
      waitForExpectedClose: waitForExpectedClose(true),
      waitForKeyboardFocus: waitForExpectedClose(true),
    });

    expect(result).toBeNull();
  });

  it("keeps a status 2 failure even for an expectsWindowClose scenario", async () => {
    const result = await injectAndResolveFailure({
      inject: () => ({ failure: "boom", status: USAGE_ERROR_STATUS }),
      scenario: injectionScenario(true),
      waitForExpectedClose: waitForExpectedClose(true),
      waitForKeyboardFocus: waitForExpectedClose(true),
    });

    expect(result).toBe("boom");
  });
});
