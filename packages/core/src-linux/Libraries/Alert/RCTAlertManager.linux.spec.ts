import { afterEach, describe, expect, it, vi } from "vitest";
import { readAlertRequests, removeAlertRequest } from "./alertRequests.ts";
import type { AlertRequest } from "./alertRequests.ts";
import { alertWithArgs } from "./RCTAlertManager.linux.ts";

afterEach(() => {
  for (const { id } of readAlertRequests()) {
    removeAlertRequest(id);
  }
});

const onlyRequest = (): AlertRequest => {
  const [request = null, extra = null] = readAlertRequests();

  if (request === null || extra !== null) {
    throw new Error("expected exactly one request");
  }

  return request;
};

const buttonsOf = (request: AlertRequest): [string, string, string][] =>
  request.buttons.map(({ key, style, text }) => [key, text, style]);

const respond = (): AlertRequest["respond"] => vi.fn<AlertRequest["respond"]>();

describe("alertWithArgs, as RCTAlertManager.mm, without buttons", () => {
  it("refuses an alert with neither a title nor a message", () => {
    const error = vi.spyOn(console, "error").mockReturnValue();

    alertWithArgs({}, respond());

    expect(readAlertRequests()).toStrictEqual([]);
    expect(error).toHaveBeenCalledWith("Must specify either an alert title, or message, or both");
  });

  it("gives an alert one OK, which is the cancel key, and keeps the caller's callback", () => {
    const callback = respond();

    alertWithArgs({ buttons: [], message: "Saved" }, callback);

    const request = onlyRequest();

    expect(buttonsOf(request)).toStrictEqual([["0", "OK", "cancel"]]);
    expect(request).toMatchObject({ cancelable: false, dismiss: null, message: "Saved", title: null, type: "default" });
    expect(request.respond).toBe(callback);
  });

  it("gives a prompt OK and Cancel, with Cancel as the cancel key", () => {
    alertWithArgs({ title: "Rename", type: "plain-text" }, respond());

    expect(buttonsOf(onlyRequest())).toStrictEqual([
      ["0", "OK", "default"],
      ["1", "Cancel", "cancel"],
    ]);
  });
});

describe("alertWithArgs, as RCTAlertManager.mm, with the caller's options", () => {
  it("keeps login-password's type, the default value and the style for the call", () => {
    const onDismiss = vi.fn<() => void>();
    const args = { cancelable: true, defaultValue: "ada", onDismiss, type: "login-password" } as const;

    alertWithArgs({ ...args, title: "Sign in", userInterfaceStyle: "dark" }, respond());

    expect(onlyRequest()).toMatchObject({
      cancelable: true,
      defaultValue: "ada",
      dismiss: onDismiss,
      type: "login-password",
      userInterfaceStyle: "dark",
    });
  });

  it("styles each button by the cancel, destructive and preferred keys, and reads a definition's first key", () => {
    const keys = { cancelButtonKey: "0", destructiveButtonKey: "1", preferredButtonKey: "2" } as const;
    const buttons = [{ "0": "Keep" }, { "1": "Delete", "9": "ignored" }, { "2": "Later" }, {}];

    alertWithArgs({ ...keys, buttons, title: "Delete?", type: "unknown", userInterfaceStyle: "sepia" }, respond());

    const request = onlyRequest();

    expect(buttonsOf(request)).toStrictEqual([
      ["0", "Keep", "cancel"],
      ["1", "Delete", "destructive"],
      ["2", "Later", "default"],
    ]);
    expect(request.buttons.map(({ isPreferred }) => isPreferred)).toStrictEqual([false, false, true]);
    expect(request).toMatchObject({ type: "default", userInterfaceStyle: "unspecified" });
  });

  it("keeps a destructive-only alert's single button destructive, with no cancel key", () => {
    alertWithArgs({ buttons: [{ "0": "Erase" }], destructiveButtonKey: "0", title: "Erase?" }, respond());

    expect(buttonsOf(onlyRequest())).toStrictEqual([["0", "Erase", "destructive"]]);
  });
});
