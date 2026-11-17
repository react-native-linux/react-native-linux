import type { AlertButtonStyle, AlertContent, AlertKeyboard, AlertRequestButton, AlertType } from "./alertRequests.ts";
import {
  addAlertRequest,
  alertKeyboard,
  mountAlertRoot,
  readAlertRequests,
  removeAlertRequest,
  responseValue,
  subscribeAlertRequests,
  visualButtonOrder,
} from "./alertRequests.ts";
import { afterEach, describe, expect, it, vi } from "vitest";

const firstRoot = 1;
const secondRoot = 2;

const button = (key: string, style: AlertButtonStyle = "default", isPreferred = false): AlertRequestButton => ({
  isPreferred,
  key,
  style,
  text: `button ${key}`,
});

const keyboardOf = (
  buttons: readonly AlertRequestButton[],
  type: AlertType = "default",
  cancelable = false,
): AlertKeyboard => alertKeyboard({ buttons, cancelable, type });

const content = (title: string): AlertContent => ({
  buttons: [button("0")],
  cancelable: false,
  defaultValue: "",
  dismiss: null,
  message: null,
  respond: vi.fn<AlertContent["respond"]>(),
  title,
  type: "default",
  userInterfaceStyle: "unspecified",
});

const unmounts = new Set<() => void>();

const mount = (rootTag: number): (() => void) => {
  const unmount = mountAlertRoot(rootTag);

  unmounts.add(unmount);

  return unmount;
};

afterEach(() => {
  for (const unmount of unmounts) {
    unmount();
  }

  unmounts.clear();

  for (const { id } of readAlertRequests()) {
    removeAlertRequest(id);
  }
});

const titlesOf = (rootTag: number | null): (string | null)[] =>
  readAlertRequests()
    .filter((request) => request.rootTag === rootTag)
    .map(({ title }) => title);

describe("alertKeyboard with buttons to choose from", () => {
  it("defaults to the preferred button, focuses it, and Escape presses cancel", () => {
    expect(keyboardOf([button("0", "cancel"), button("1", "default", true), button("2")])).toStrictEqual({
      defaultKey: "1",
      escape: { key: "0", kind: "press" },
      initialFocus: { key: "1", kind: "button" },
    });
  });

  it("never defaults to a preferred destructive button: the last plain one instead", () => {
    expect(
      keyboardOf([button("0"), button("1"), button("2", "destructive", true), button("3", "cancel")]),
    ).toStrictEqual({
      defaultKey: "1",
      escape: { key: "3", kind: "press" },
      initialFocus: { key: "1", kind: "button" },
    });
  });

  it("focuses cancel when no safe default exists", () => {
    expect(keyboardOf([button("0", "cancel"), button("1", "destructive")])).toStrictEqual({
      defaultKey: null,
      escape: { key: "0", kind: "press" },
      initialFocus: { key: "0", kind: "button" },
    });
  });

  it("focuses a prompt's field whatever its buttons", () => {
    expect(keyboardOf([button("0"), button("1", "cancel")], "login-password")).toStrictEqual({
      defaultKey: "0",
      escape: { key: "1", kind: "press" },
      initialFocus: { kind: "field" },
    });
  });
});

describe("alertKeyboard with one button or no safe one", () => {
  it("activates a lone acknowledgement on Escape", () => {
    expect(keyboardOf([button("0")])).toStrictEqual({
      defaultKey: "0",
      escape: { key: "0", kind: "press" },
      initialFocus: { key: "0", kind: "button" },
    });
  });

  it("focuses the card of a destructive-only alert, which Escape dismisses only when cancelable", () => {
    expect(keyboardOf([button("0", "destructive")], "default", true)).toStrictEqual({
      defaultKey: null,
      escape: { kind: "dismiss" },
      initialFocus: { kind: "card" },
    });
    expect(keyboardOf([button("0", "destructive"), button("1", "destructive")])).toStrictEqual({
      defaultKey: null,
      escape: { kind: "none" },
      initialFocus: { kind: "card" },
    });
  });
});

describe("alertKeyboard for a prompt", () => {
  it("never commits a prompt on Escape, even with one acknowledgement", () => {
    expect(keyboardOf([button("0")], "plain-text")).toStrictEqual({
      defaultKey: "0",
      escape: { kind: "none" },
      initialFocus: { kind: "field" },
    });
    expect(keyboardOf([button("0")], "secure-text", true).escape).toStrictEqual({ kind: "dismiss" });
  });
});

describe("responseValue", () => {
  it("calls back with nothing for an alert, the text for a prompt, and the credentials for login-password", () => {
    expect(responseValue("default", "ada", "secret")).toStrictEqual([]);
    expect(responseValue("plain-text", "ada", "secret")).toStrictEqual(["ada"]);
    expect(responseValue("secure-text", "hunter2", "")).toStrictEqual(["hunter2"]);
    expect(responseValue("login-password", "ada", "secret")).toStrictEqual([{ login: "ada", password: "secret" }]);
  });
});

describe("visualButtonOrder", () => {
  it("moves cancel to the start and keeps every button's key", () => {
    const buttons = [button("0"), button("1", "destructive"), button("2", "cancel")];

    expect(visualButtonOrder(buttons).map(({ key }) => key)).toStrictEqual(["2", "0", "1"]);
    expect(visualButtonOrder([button("0"), button("1")])).toStrictEqual([button("0"), button("1")]);
  });
});

describe("the request store before and after a root mounts", () => {
  it("keeps a request raised before any root mounts and hands it to the first root", () => {
    addAlertRequest(content("early"));

    expect(titlesOf(null)).toStrictEqual(["early"]);

    mount(firstRoot);

    expect(titlesOf(firstRoot)).toStrictEqual(["early"]);
  });

  it("targets the first mounted root, stacks newest last, and removes one request by id", () => {
    mount(firstRoot);
    addAlertRequest(content("first"));
    mount(secondRoot);
    addAlertRequest(content("second"));

    expect(titlesOf(firstRoot)).toStrictEqual(["first", "second"]);
    expect(titlesOf(secondRoot)).toStrictEqual([]);

    const [first] = readAlertRequests();

    removeAlertRequest(first?.id ?? Number.NaN);

    expect(titlesOf(firstRoot)).toStrictEqual(["second"]);
  });
});

describe("the request store across roots", () => {
  it("drops a destroyed root's requests and targets the next mounted root", () => {
    const unmountFirst = mount(firstRoot);

    mount(secondRoot);
    addAlertRequest(content("on first"));
    unmountFirst();
    addAlertRequest(content("on second"));

    expect(readAlertRequests().map(({ title, rootTag }) => [title, rootTag])).toStrictEqual([
      ["on second", secondRoot],
    ]);
  });

  it("notifies subscribers of every change until they unsubscribe", () => {
    const listener = vi.fn<() => void>();
    const unsubscribe = subscribeAlertRequests(listener);

    addAlertRequest(content("one"));
    unsubscribe();
    addAlertRequest(content("two"));

    expect(listener).toHaveBeenCalledOnce();
  });
});
