type AlertType = "default" | "plain-text" | "secure-text" | "login-password";

type AlertButtonStyle = "default" | "cancel" | "destructive";

interface AlertCredentials {
  readonly login: string;
  readonly password: string;
}

type AlertValue = string | AlertCredentials;

interface AlertRequestButton {
  /** Upstream's key: the button's index in the caller's array, which is what the callback receives. */
  readonly key: string;
  readonly text: string;
  readonly style: AlertButtonStyle;
  readonly isPreferred: boolean;
}

interface AlertContent {
  readonly title: string | null;
  readonly message: string | null;
  readonly type: AlertType;
  readonly defaultValue: string;
  readonly buttons: readonly AlertRequestButton[];
  readonly cancelable: boolean;
  readonly userInterfaceStyle: "light" | "dark" | "unspecified";
  /** RCTAlertManager.mm's callback: the key alone for a `default` alert, the key and the field's value otherwise. */
  readonly respond: (key: string, ...value: [] | [AlertValue]) => void;
  readonly dismiss: (() => void) | null;
}

interface AlertRequest extends AlertContent {
  readonly id: number;
  /** The root whose host draws it; null until a root mounts. */
  readonly rootTag: number | null;
}

type Listener = () => void;

const idStep = 1;

/**
 * ADR-0005's request store: module state, so an alert raised before any root mounts waits for the first one. A
 * request targets the first mounted root, and dies with it.
 */
let requests: readonly AlertRequest[] = [];
let mountedRoots: readonly number[] = [];
let lastRequestId = 0;
const listeners = new Set<Listener>();

const publish = (next: readonly AlertRequest[]): void => {
  requests = next;

  for (const listener of listeners) {
    listener();
  }
};

const addAlertRequest = (content: AlertContent): void => {
  const [firstRoot = null] = mountedRoots;

  lastRequestId += idStep;
  publish([...requests, { ...content, id: lastRequestId, rootTag: firstRoot }]);
};

const removeAlertRequest = (id: number): void => {
  publish(requests.filter((request) => request.id !== id));
};

/** Registers a root's host and hands it every request still waiting for a root; the returned call unmounts it. */
const mountAlertRoot = (rootTag: number): (() => void) => {
  mountedRoots = [...mountedRoots, rootTag];
  publish(requests.map((request) => (request.rootTag === null ? { ...request, rootTag } : request)));

  return () => {
    mountedRoots = mountedRoots.filter((mounted) => mounted !== rootTag);
    publish(requests.filter((request) => request.rootTag !== rootTag));
  };
};

const subscribeAlertRequests = (listener: Listener): (() => void) => {
  listeners.add(listener);

  return () => {
    listeners.delete(listener);
  };
};

/** Every pending request, oldest first, so the newest draws on top. */
const readAlertRequests = (): readonly AlertRequest[] => requests;

type AlertFocus =
  | { readonly kind: "field" }
  | { readonly kind: "button"; readonly key: string }
  | { readonly kind: "card" };

type AlertEscape =
  | { readonly kind: "press"; readonly key: string }
  | { readonly kind: "dismiss" }
  | { readonly kind: "none" };

interface AlertKeyboard {
  /** What Enter in a prompt's field presses. Never a destructive button. */
  readonly defaultKey: string | null;
  readonly initialFocus: AlertFocus;
  readonly escape: AlertEscape;
}

interface KeyboardInput {
  readonly buttons: readonly AlertRequestButton[];
  readonly type: AlertType;
  readonly cancelable: boolean;
}

const loneButtonCount = 1;

const defaultButtonKey = (buttons: readonly AlertRequestButton[]): string | null => {
  const preferred = buttons.find((button) => button.isPreferred && button.style !== "destructive");

  return preferred?.key ?? buttons.findLast((button) => button.style === "default")?.key ?? null;
};

const initialFocus = (type: AlertType, defaultKey: string | null, cancelKey: string | null): AlertFocus => {
  if (type !== "default") {
    return { kind: "field" };
  }

  const key = defaultKey ?? cancelKey;

  return key === null ? { kind: "card" } : { key, kind: "button" };
};

const escapeAction = ({ buttons, cancelable }: KeyboardInput, cancelKey: string | null): AlertEscape => {
  const acknowledgement =
    buttons.length === loneButtonCount ? (buttons.find((button) => button.style !== "destructive") ?? null) : null;

  if (acknowledgement !== null) {
    return { key: acknowledgement.key, kind: "press" };
  }

  if (cancelKey !== null) {
    return { key: cancelKey, kind: "press" };
  }

  return cancelable ? { kind: "dismiss" } : { kind: "none" };
};

/** ADR-0005's keyboard table: Enter's default, the focus `onShow` applies, and what Escape does. */
const alertKeyboard = (input: KeyboardInput): AlertKeyboard => {
  const defaultKey = defaultButtonKey(input.buttons);
  const cancelKey = input.buttons.find((button) => button.style === "cancel")?.key ?? null;

  return {
    defaultKey,
    escape: escapeAction(input, cancelKey),
    initialFocus: initialFocus(input.type, defaultKey, cancelKey),
  };
};

/** The row's order: the cancel button first, as UIAlertController and the GNOME HIG place it. Keys never change. */
const visualButtonOrder = (buttons: readonly AlertRequestButton[]): readonly AlertRequestButton[] => [
  ...buttons.filter((button) => button.style === "cancel"),
  ...buttons.filter((button) => button.style !== "cancel"),
];

export type { AlertButtonStyle, AlertContent, AlertKeyboard, AlertRequest, AlertRequestButton, AlertType, AlertValue };
export {
  addAlertRequest,
  alertKeyboard,
  mountAlertRoot,
  readAlertRequests,
  removeAlertRequest,
  subscribeAlertRequests,
  visualButtonOrder,
};
