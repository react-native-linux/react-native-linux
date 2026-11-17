import { beforeEach, describe, expect, it, vi } from "vitest";
import Alert from "./Alert.linux.ts";

const { alertWithArgs } = vi.hoisted(() => ({ alertWithArgs: vi.fn() }));

vi.mock("./RCTAlertManager.linux.ts", () => ({ alertWithArgs }));

type Respond = (key: string, ...value: [] | [unknown]) => void;

const lastCall = (): { readonly args: unknown; readonly respond: Respond } => {
  const [args, respond] = alertWithArgs.mock.lastCall ?? [];

  return { args, respond };
};

const noKeys = { cancelButtonKey: null, destructiveButtonKey: null, preferredButtonKey: null } as const;

beforeEach(() => {
  alertWithArgs.mockClear();
});

describe("Alert.alert on Linux, upstream's iOS branch", () => {
  it("keys the buttons by index, drops a textless last one, and passes Android's cancelable and onDismiss", () => {
    const onDismiss = vi.fn<() => void>();
    const remove = vi.fn<() => void>();
    const buttons = [
      { style: "cancel", text: "Keep" },
      { isPreferred: true, onPress: remove, style: "destructive", text: "Delete" },
      { onPress: null },
    ] as const;

    Alert.alert("Delete?", "", buttons, { cancelable: true, onDismiss, userInterfaceStyle: "dark" });

    expect(lastCall().args).toStrictEqual({
      buttons: [{ "0": "Keep" }, { "1": "Delete" }],
      cancelButtonKey: "0",
      cancelable: true,
      defaultValue: "",
      destructiveButtonKey: "1",
      keyboardType: "",
      message: null,
      onDismiss,
      preferredButtonKey: "1",
      title: "Delete?",
      type: "default",
      userInterfaceStyle: "dark",
    });
  });

  it("calls the pressed button's callback, with nothing for a default alert", () => {
    const remove = vi.fn<() => void>();

    Alert.alert("Delete?", "Gone for good", [{ text: "Keep" }, { onPress: remove, text: "Delete" }, { onPress: null }]);
    lastCall().respond("1");
    lastCall().respond("2");

    expect(remove).toHaveBeenCalledWith();
  });
});

describe("Alert.alert on Linux, for what the caller leaves out", () => {
  it("keeps a middle button with no text and passes nulls for what the caller left out", () => {
    Alert.alert(null, null, [{ text: "A" }, {}, { text: "C" }]);

    expect(lastCall().args).toStrictEqual({
      ...noKeys,
      buttons: [{ "0": "A" }, { "1": "" }, { "2": "C" }],
      cancelable: null,
      defaultValue: "",
      keyboardType: "",
      message: null,
      onDismiss: null,
      title: "",
      type: "default",
      userInterfaceStyle: null,
    });
  });
});

describe("Alert.prompt on Linux, upstream's iOS branch", () => {
  it("prompts as plain text by default and hands a callback the field's value", () => {
    const onText = vi.fn<(value?: unknown) => void>();

    Alert.prompt("Rename", "New name", onText);

    expect(lastCall().args).toMatchObject({ buttons: [], defaultValue: null, message: "New name", type: "plain-text" });

    lastCall().respond("0", "renamed");

    expect(onText).toHaveBeenCalledWith("renamed");
  });

  it("passes the type, default value and keyboard type it is given, and a null type as none", () => {
    Alert.prompt("Unlock", "Password", null, "secure-text", "draft", "number-pad");

    expect(lastCall().args).toMatchObject({ defaultValue: "draft", keyboardType: "number-pad", type: "secure-text" });

    Alert.prompt("Sign in", null, null, null);

    expect(lastCall().args).toMatchObject({ buttons: [], keyboardType: null, message: null, type: null });
  });
});
