import type { AlertButtonStyle, AlertContent, AlertRequestButton, AlertType } from "./alertRequests.ts";
import { addAlertRequest } from "./alertRequests.ts";

type ButtonDefinitions = readonly Readonly<Record<string, string>>[];

/** NativeAlertManager's `Args`, plus Android's `cancelable` and `onDismiss`, which ADR-0005's Escape rules need. */
interface AlertArgs {
  readonly title?: string | null;
  readonly message?: string | null;
  readonly buttons?: ButtonDefinitions | null;
  readonly type?: string | null;
  readonly defaultValue?: string | null;
  readonly cancelButtonKey?: string | null;
  readonly destructiveButtonKey?: string | null;
  readonly preferredButtonKey?: string | null;
  readonly keyboardType?: string | null;
  readonly userInterfaceStyle?: string | null;
  readonly cancelable?: boolean | null;
  readonly onDismiss?: (() => void) | null;
}

const alertTypes: readonly AlertType[] = ["default", "plain-text", "secure-text", "login-password"];
const noButtons = 0;
const lastIndexOffset = 1;
const definitionKeyCount = 1;

const readType = (type: string | null): AlertType => alertTypes.find((known) => known === type) ?? "default";

const readUserInterfaceStyle = (style: string | null): AlertContent["userInterfaceStyle"] =>
  style === "light" || style === "dark" ? style : "unspecified";

/** With no buttons, a plain alert gets OK, and a prompt OK and Cancel; the last one is the cancel key. */
const defaultButtons = (type: AlertType): ButtonDefinitions =>
  type === "default" ? [{ "0": "OK" }] : [{ "0": "OK" }, { "1": "Cancel" }];

const buttonStyle = (key: string, cancelKey: string | null, destructiveKey: string | null): AlertButtonStyle => {
  if (key === cancelKey) {
    return "cancel";
  }

  return key === destructiveKey ? "destructive" : "default";
};

const readButtons = (args: AlertArgs, type: AlertType): readonly AlertRequestButton[] => {
  const given = args.buttons ?? [];
  const definitions = given.length > noButtons ? given : defaultButtons(type);
  const cancelKey =
    given.length > noButtons ? (args.cancelButtonKey ?? null) : String(definitions.length - lastIndexOffset);

  return definitions.flatMap((definition) =>
    Object.entries(definition)
      .slice(noButtons, definitionKeyCount)
      .map(([key, text]) => ({
        isPreferred: key === args.preferredButtonKey,
        key,
        style: buttonStyle(key, cancelKey, args.destructiveButtonKey ?? null),
        text,
      })),
  );
};

/**
 * `alertWithArgs` of React/CoreModules/RCTAlertManager.mm, in JavaScript: the same defaults, keys and callback
 * shapes, handed to the request store that `AlertHost` draws from instead of to a `UIAlertController`.
 */
const alertWithArgs = (args: AlertArgs, onResult: AlertContent["respond"]): void => {
  const title = args.title ?? null;
  const message = args.message ?? null;

  if (title === null && message === null) {
    // oxlint-disable-next-line eslint/no-console -- RCTAlertManager.mm's RCTLogError, which LogBox shows in development.
    console.error("Must specify either an alert title, or message, or both");

    return;
  }

  const type = readType(args.type ?? null);

  addAlertRequest({
    buttons: readButtons(args, type),
    cancelable: args.cancelable === true,
    defaultValue: args.defaultValue ?? "",
    dismiss: args.onDismiss ?? null,
    message,
    respond: onResult,
    title,
    type,
    userInterfaceStyle: readUserInterfaceStyle(args.userInterfaceStyle ?? null),
  });
};

export { alertWithArgs };
