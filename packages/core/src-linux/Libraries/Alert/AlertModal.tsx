import type { AlertKeyboard, AlertRequest, AlertRequestButton, AlertValue } from "./alertRequests.ts";
import type { ComponentRef, RefObject } from "react";
import { Modal, Pressable, ScrollView, StyleSheet, Text, TextInput, View, useColorScheme } from "react-native";
import { alertKeyboard, removeAlertRequest, visualButtonOrder } from "./alertRequests.ts";
import { useRef, useState } from "react";

const palettes = {
  dark: {
    accent: "#3b82f6",
    accentText: "#ffffff",
    card: "#2b2f36",
    destructive: "#ff7b72",
    field: "#1f2328",
    neutral: "#3d434c",
    secondaryText: "#b1bac4",
    separator: "#4b525c",
    text: "#f0f3f6",
  },
  light: {
    accent: "#2563eb",
    accentText: "#ffffff",
    card: "#ffffff",
    destructive: "#cf222e",
    field: "#ffffff",
    neutral: "#eef1f4",
    secondaryText: "#57606a",
    separator: "#d0d7de",
    text: "#1f2328",
  },
} as const;

const styles = StyleSheet.create({
  button: { borderRadius: 6, minWidth: 80, paddingHorizontal: 16, paddingVertical: 8 },
  buttonText: { fontSize: 14, fontWeight: "600", textAlign: "center" },
  card: { borderRadius: 12, gap: 12, maxHeight: "100%", maxWidth: 400, padding: 20, width: "100%" },
  field: { borderRadius: 6, borderWidth: 1, fontSize: 14, height: 36, paddingHorizontal: 8 },
  message: { fontSize: 14, lineHeight: 20 },
  messageScroll: { flexGrow: 0, flexShrink: 1 },
  row: { flexDirection: "row", flexWrap: "wrap", gap: 8, justifyContent: "flex-end" },
  scrim: {
    alignItems: "center",
    backgroundColor: "rgba(0, 0, 0, 0.4)",
    flex: 1,
    justifyContent: "center",
    padding: 24,
  },
  title: { fontSize: 17, fontWeight: "700" },
});

interface Card {
  readonly request: AlertRequest;
  readonly keyboard: AlertKeyboard;
  readonly palette: (typeof palettes)[keyof typeof palettes];
  readonly login: string;
  readonly password: string;
  readonly setLogin: (text: string) => void;
  readonly setPassword: (text: string) => void;
  readonly fieldTarget: RefObject<ComponentRef<typeof TextInput> | null>;
  readonly viewTarget: RefObject<ComponentRef<typeof View> | null>;
  readonly press: (key: string) => void;
}

const fieldValue = (request: AlertRequest, login: string, password: string): [] | [AlertValue] => {
  if (request.type === "default") {
    return [];
  }

  return [request.type === "login-password" ? { login, password } : login];
};

const useCard = (request: AlertRequest): Card => {
  const colorScheme = useColorScheme();
  const [login, setLogin] = useState(request.defaultValue);
  const [password, setPassword] = useState("");
  const fieldTarget = useRef<ComponentRef<typeof TextInput>>(null);
  const viewTarget = useRef<ComponentRef<typeof View>>(null);
  const style = request.userInterfaceStyle === "unspecified" ? colorScheme : request.userInterfaceStyle;

  return {
    fieldTarget,
    keyboard: alertKeyboard(request),
    login,
    palette: palettes[style === "dark" ? "dark" : "light"],
    password,
    press: (key) => {
      removeAlertRequest(request.id);
      request.respond(key, ...fieldValue(request, login, password));
    },
    request,
    setLogin,
    setPassword,
    viewTarget,
  };
};

const pressDefault = (card: Card): void => {
  if (card.keyboard.defaultKey !== null) {
    card.press(card.keyboard.defaultKey);
  }
};

const escape = (card: Card): void => {
  const action = card.keyboard.escape;

  if (action.kind === "press") {
    card.press(action.key);
  } else if (action.kind === "dismiss") {
    removeAlertRequest(card.request.id);
    card.request.dismiss?.();
  }
};

const focusInitial = (card: Card): void => {
  if (card.keyboard.initialFocus.kind === "field") {
    card.fieldTarget.current?.focus();
  } else {
    card.viewTarget.current?.focus();
  }
};

const isInitialButton = (card: Card, button: AlertRequestButton): boolean =>
  card.keyboard.initialFocus.kind === "button" && card.keyboard.initialFocus.key === button.key;

const buttonTextColor = (card: Card, button: AlertRequestButton): string => {
  if (button.key === card.keyboard.defaultKey) {
    return card.palette.accentText;
  }

  return button.style === "destructive" ? card.palette.destructive : card.palette.text;
};

const renderButton = (card: Card, button: AlertRequestButton): React.JSX.Element => (
  <Pressable
    accessibilityRole="button"
    aria-label={button.text}
    key={button.key}
    onPress={() => card.press(button.key)}
    ref={isInitialButton(card, button) ? card.viewTarget : null}
    style={[
      styles.button,
      { backgroundColor: button.key === card.keyboard.defaultKey ? card.palette.accent : card.palette.neutral },
    ]}
  >
    <Text style={[styles.buttonText, { color: buttonTextColor(card, button) }]}>{button.text}</Text>
  </Pressable>
);

const renderField = (card: Card, secondField: boolean): React.JSX.Element => {
  const isPassword = secondField || card.request.type === "secure-text";
  const placeholders = { default: "", "login-password": "Login", "plain-text": "", "secure-text": "Password" };

  return (
    <TextInput
      onChangeText={secondField ? card.setPassword : card.setLogin}
      onSubmitEditing={() => pressDefault(card)}
      placeholder={secondField ? "Password" : placeholders[card.request.type]}
      placeholderTextColor={card.palette.secondaryText}
      ref={secondField ? null : card.fieldTarget}
      secureTextEntry={isPassword}
      style={[
        styles.field,
        { backgroundColor: card.palette.field, borderColor: card.palette.separator, color: card.palette.text },
      ]}
      value={secondField ? card.password : card.login}
    />
  );
};

const renderMessage = (card: Card, message: string): React.JSX.Element => (
  <ScrollView style={styles.messageScroll}>
    <Text style={[styles.message, { color: card.palette.secondaryText }]}>{message}</Text>
  </ScrollView>
);

const renderCard = (card: Card): React.JSX.Element => {
  const { request } = card;

  return (
    <View
      accessibilityHint={request.message ?? ""}
      accessible={card.keyboard.initialFocus.kind === "card"}
      aria-label={request.title ?? ""}
      ref={card.keyboard.initialFocus.kind === "card" ? card.viewTarget : null}
      role="alertdialog"
      style={[styles.card, { backgroundColor: card.palette.card }]}
    >
      {request.title === null || request.title === "" ? null : (
        <Text style={[styles.title, { color: card.palette.text }]}>{request.title}</Text>
      )}
      {request.message === null ? null : renderMessage(card, request.message)}
      {request.type === "default" ? null : renderField(card, false)}
      {request.type === "login-password" ? renderField(card, true) : null}
      <View style={styles.row}>{visualButtonOrder(request.buttons).map((button) => renderButton(card, button))}</View>
    </View>
  );
};

/** One alert: a desktop dialog card over a scrim, on a transparent `Modal` (ADR-0005). */
const AlertModal = ({ request }: { readonly request: AlertRequest }): React.JSX.Element => {
  const card = useCard(request);

  return (
    <Modal
      animationType="none"
      onRequestClose={() => escape(card)}
      onShow={() => focusInitial(card)}
      transparent
      visible
    >
      <View style={styles.scrim}>{renderCard(card)}</View>
    </Modal>
  );
};

export default AlertModal;
