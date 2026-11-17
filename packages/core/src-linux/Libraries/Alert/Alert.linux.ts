import type { AlertButton, AlertOptions, AlertType } from "react-native";
import { alertWithArgs } from "./RCTAlertManager.linux.ts";

type Callback = ((text: string) => void) | AlertButton["onPress"];

const NOT_FOUND = -1;
const lastIndexOffset = 1;

const lastKey = (buttons: readonly AlertButton[], predicate: (button: AlertButton) => boolean): string | null => {
  const index = buttons.findLastIndex((button) => predicate(button));

  return index === NOT_FOUND ? null : String(index);
};

/** A last button with no text is dropped, as upstream does; its callback keeps its index. */
const buttonDefinitions = (buttons: readonly AlertButton[]): Record<string, string>[] =>
  buttons.flatMap((button, index) =>
    (button.text ?? "") !== "" || index < buttons.length - lastIndexOffset ? [{ [index]: button.text ?? "" }] : [],
  );

/**
 * Upstream's `prompt` with its iOS branch taken unconditionally. The one Linux deviation is that it also passes
 * Android's `cancelable` and `onDismiss`, which ADR-0005's Escape rules need.
 */
// oxlint-disable-next-line eslint/max-params -- Upstream's public signature.
const prompt = (
  title: string | null | undefined,
  message?: string | null,
  callbackOrButtons?: ((text: string) => void) | readonly AlertButton[] | null,
  type: AlertType | null = "plain-text",
  defaultValue?: string,
  keyboardType?: string,
  options?: AlertOptions,
): void => {
  const buttons = typeof callbackOrButtons === "function" ? [] : (callbackOrButtons ?? []);
  const callbacks: readonly Callback[] =
    typeof callbackOrButtons === "function" ? [callbackOrButtons] : buttons.map((button) => button.onPress);

  alertWithArgs(
    {
      buttons: buttonDefinitions(buttons),
      cancelButtonKey: lastKey(buttons, (button) => button.style === "cancel"),
      cancelable: options?.cancelable ?? null,
      defaultValue: defaultValue ?? null,
      destructiveButtonKey: lastKey(buttons, (button) => button.style === "destructive"),
      keyboardType: keyboardType ?? null,
      message: message === "" ? null : (message ?? null),
      onDismiss: options?.onDismiss ?? null,
      preferredButtonKey: lastKey(buttons, (button) => button.isPreferred === true),
      title: title ?? "",
      type,
      userInterfaceStyle: options?.userInterfaceStyle ?? null,
    },
    (key, ...value) => {
      const callback = callbacks[Number(key)];

      // Upstream hands login-password's credentials to a callback typed for text, and `Function` takes any arguments.
      if (typeof callback === "function") {
        Reflect.apply(callback, null, value);
      }
    },
  );
};

// oxlint-disable-next-line eslint/max-params -- Upstream's public signature.
const alert = (
  title: string | null | undefined,
  message?: string | null,
  buttons?: readonly AlertButton[],
  options?: AlertOptions,
): void => {
  prompt(title, message, buttons, "default", "", "", options);
};

const Alert = { alert, prompt };

export default Alert;
