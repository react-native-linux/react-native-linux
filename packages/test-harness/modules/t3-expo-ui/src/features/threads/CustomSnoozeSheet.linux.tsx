import {
  localSnoozeDate,
  localSnoozeTime,
  resolveCustomSnooze,
  type CustomSnoozeInput,
} from "@t3tools/client-runtime/state/thread-settled";
import { useRef, useState } from "react";
import { Pressable, TextInput, View, useWindowDimensions } from "react-native";

import { AppText } from "../../components/ui/AppText";
import { IconButton } from "../../components/ui/IconButton";
import { OverlayPortal } from "../../components/ui/OverlayPortal";
import { useAppearancePreferences } from "../settings/appearance/AppearancePreferencesProvider";

const modes = [
  { value: "date", label: "Date and time" },
  { value: "duration", label: "Duration" },
] as const;
const units = [
  { value: "minutes", label: "Minutes" },
  { value: "hours", label: "Hours" },
  { value: "days", label: "Days" },
] as const;

type Colors = ReturnType<typeof useAppearancePreferences>["themeVariables"];

// react-native-linux: SwiftUI's popover of wheels, drawn with Views. The date and time are typed
// as YYYY-MM-DD and HH:MM, which resolveCustomSnooze already validates.
export function CustomSnoozeSheet(props: {
  readonly onClose: () => void;
  readonly onSnooze: (snoozedUntil: string) => void;
}) {
  const { width } = useWindowDimensions();
  const { themeVariables: colors } = useAppearancePreferences();
  const [mode, setMode] = useState<CustomSnoozeInput["mode"]>("date");
  const [initial] = useState(() => new Date(Date.now() + 3_600_000));
  // The fields own their text; refs give submit the latest edit even when it arrives in the same event batch.
  const date = useRef(localSnoozeDate(initial));
  const time = useRef(localSnoozeTime(initial));
  const [amount, setAmount] = useState(2);
  const [unit, setUnit] = useState<"minutes" | "hours" | "days">("hours");
  const [error, setError] = useState<string | null>(null);
  const edit =
    <Value,>(set: (value: Value) => void) =>
    (value: Value) => {
      set(value);
      setError(null);
    };

  const submit = () => {
    const input: CustomSnoozeInput =
      mode === "date"
        ? { mode, date: date.current, time: time.current }
        : { mode, amount: String(amount), unit };
    const snoozedUntil = resolveCustomSnooze(input, new Date());
    if (!snoozedUntil) {
      setError(
        mode === "date" ? "Choose a date and time in the future." : "Enter a positive duration.",
      );
      return;
    }
    props.onSnooze(snoozedUntil);
    props.onClose();
  };

  const fieldStyle = {
    flex: 1,
    height: 40,
    paddingHorizontal: 12,
    borderRadius: 10,
    borderWidth: 1,
    borderColor: colors["--color-border"],
    color: colors["--color-foreground"],
    fontVariant: ["tabular-nums" as const],
  };

  return (
    <OverlayPortal>
      <View
        style={{
          position: "absolute",
          top: 0,
          right: 0,
          bottom: 0,
          left: 0,
          alignItems: "center",
          justifyContent: "center",
          backgroundColor: "rgba(0, 0, 0, 0.32)",
        }}
      >
        <Pressable
          accessibilityLabel="Cancel custom snooze"
          onPress={props.onClose}
          style={{ position: "absolute", top: 0, right: 0, bottom: 0, left: 0 }}
        />
        <View
          accessibilityViewIsModal
          style={{
            width: Math.min(360, width - 32),
            gap: 16,
            padding: 24,
            borderRadius: 28,
            backgroundColor: colors["--color-card-alt"],
          }}
        >
          <AppText textRole="title" style={{ color: colors["--color-foreground"] }}>
            Custom snooze
          </AppText>
          <Choice
            colors={colors}
            label="Snooze mode"
            options={modes}
            selected={mode}
            onSelect={edit(setMode)}
          />
          {mode === "date" ? (
            <View style={{ flexDirection: "row", gap: 8 }}>
              <TextInput
                accessibilityLabel="Snooze date"
                placeholder="YYYY-MM-DD"
                defaultValue={date.current}
                onChangeText={edit((text: string) => {
                  date.current = text;
                })}
                onSubmitEditing={submit}
                style={fieldStyle}
              />
              <TextInput
                accessibilityLabel="Snooze time"
                placeholder="HH:MM"
                defaultValue={time.current}
                onChangeText={edit((text: string) => {
                  time.current = text;
                })}
                onSubmitEditing={submit}
                style={fieldStyle}
              />
            </View>
          ) : (
            <>
              <View
                style={{ flexDirection: "row", alignItems: "center", justifyContent: "center" }}
              >
                <IconButton
                  icon="minus"
                  accessibilityLabel="Decrease duration"
                  disabled={amount <= 1}
                  onPress={() => edit(setAmount)(Math.max(1, amount - 1))}
                />
                <AppText
                  textRole="title"
                  style={{
                    minWidth: 48,
                    textAlign: "center",
                    fontVariant: ["tabular-nums"],
                    color: colors["--color-foreground"],
                  }}
                >
                  {amount}
                </AppText>
                <IconButton
                  icon="plus"
                  accessibilityLabel="Increase duration"
                  disabled={amount >= 99}
                  onPress={() => edit(setAmount)(Math.min(99, amount + 1))}
                />
              </View>
              <Choice
                colors={colors}
                label="Duration unit"
                options={units}
                selected={unit}
                onSelect={edit(setUnit)}
              />
            </>
          )}
          {error ? (
            <AppText textRole="footnote" style={{ color: colors["--color-danger-foreground"] }}>
              {error}
            </AppText>
          ) : null}
          <View style={{ flexDirection: "row", justifyContent: "flex-end", gap: 8 }}>
            <TextButton colors={colors} label="Cancel" onPress={props.onClose} />
            <TextButton colors={colors} label="Snooze" prominent onPress={submit} />
          </View>
        </View>
      </View>
    </OverlayPortal>
  );
}

function Choice<Value extends string>(props: {
  readonly colors: Colors;
  readonly label: string;
  readonly options: readonly { readonly value: Value; readonly label: string }[];
  readonly selected: Value;
  readonly onSelect: (value: Value) => void;
}) {
  return (
    <View
      accessibilityRole="radiogroup"
      accessibilityLabel={props.label}
      style={{
        flexDirection: "row",
        padding: 2,
        borderRadius: 10,
        backgroundColor: props.colors["--color-subtle"],
      }}
    >
      {props.options.map((option) => {
        const selected = option.value === props.selected;
        return (
          <Pressable
            key={option.value}
            accessibilityRole="radio"
            accessibilityState={{ checked: selected }}
            onPress={() => props.onSelect(option.value)}
            style={{
              flex: 1,
              alignItems: "center",
              paddingVertical: 6,
              borderRadius: 8,
              backgroundColor: selected ? props.colors["--color-card"] : "transparent",
            }}
          >
            <AppText textRole="footnote" style={{ color: props.colors["--color-foreground"] }}>
              {option.label}
            </AppText>
          </Pressable>
        );
      })}
    </View>
  );
}

function TextButton(props: {
  readonly colors: Colors;
  readonly label: string;
  readonly prominent?: boolean;
  readonly onPress: () => void;
}) {
  return (
    <Pressable
      accessibilityRole="button"
      onPress={props.onPress}
      style={{
        paddingHorizontal: 16,
        paddingVertical: 10,
        borderRadius: 20,
        backgroundColor: props.prominent ? props.colors["--color-primary"] : "transparent",
      }}
    >
      <AppText
        textRole="body"
        style={{
          color: props.prominent
            ? props.colors["--color-primary-foreground"]
            : props.colors["--color-primary-text"],
        }}
      >
        {props.label}
      </AppText>
    </Pressable>
  );
}
