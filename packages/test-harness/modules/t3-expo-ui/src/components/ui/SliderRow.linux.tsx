import { useState } from "react";
import { StyleSheet, View, type GestureResponderEvent } from "react-native";

import { AppText } from "./AppText";
import { SymbolView } from "./AppSymbol";
import { rangeFraction, snapToStep, valueAtOffset } from "./controlValues.linux";
import type { SliderRowProps } from "./SliderRow.types";
import { useControlTheme } from "./useControlTheme";

export type { SliderRowProps } from "./SliderRow.types";

const SLIDER_HEIGHT = 44;
const TRACK_HEIGHT = 4;
const THUMB_SIZE = 24;
const TRACK_OPACITY = 0.16;
const KEY_STEPS: Readonly<Record<string, number>> = { ArrowLeft: -1, ArrowDown: -1, ArrowRight: 1, ArrowUp: 1 };

// react-native-linux: SwiftUI's Slider, drawn with Views. A drag moves the thumb and commits
// the stepped value on release, as the iOS row commits when editing ends; the arrow keys step it.
export function SliderRow(props: SliderRowProps) {
  const { colors } = useControlTheme();
  const [width, setWidth] = useState(0);
  // The value of the drag in progress; null between drags.
  const [draft, setDraft] = useState<number | null>(null);
  const trackWidth = Math.max(0, width - THUMB_SIZE);
  const valueAt = (event: GestureResponderEvent) =>
    snapToStep(
      valueAtOffset(event.nativeEvent.locationX - THUMB_SIZE / 2, trackWidth, props.min, props.max),
      props.min,
      props.max,
      props.step,
    );
  const commit = (value: number) => {
    setDraft(null);
    if (props.disabled) return;
    const next = snapToStep(value, props.min, props.max, props.step);
    if (next !== props.value) props.onChange(next);
  };
  const fraction = rangeFraction(draft ?? props.value, props.min, props.max);
  return (
    <View style={{ gap: 4, padding: 16, opacity: props.disabled ? 0.45 : 1 }}>
      <View style={{ flexDirection: "row", alignItems: "center", gap: 16 }}>
        <SymbolView name={props.icon} size={22} tintColorClassName="accent-icon" />
        <AppText textRole="headline" style={{ flex: 1, color: colors["--color-foreground"] }}>
          {props.label}
        </AppText>
        <AppText
          textRole="body"
          style={{ fontWeight: "500", color: colors["--color-foreground-muted"] }}
        >
          {props.valueLabel}
        </AppText>
      </View>
      <View style={{ flexDirection: "row", alignItems: "center", gap: 12 }}>
        {props.minimumIcon ? (
          <SymbolView name={props.minimumIcon} size={15} tintColorClassName="accent-icon-muted" />
        ) : null}
        <View
          accessible
          accessibilityLabel={props.label}
          accessibilityRole="adjustable"
          accessibilityState={{ disabled: Boolean(props.disabled) }}
          accessibilityValue={{
            min: props.min,
            max: props.max,
            now: props.value,
            text: props.valueLabel,
          }}
          accessibilityActions={[{ name: "increment" }, { name: "decrement" }]}
          onAccessibilityAction={({ nativeEvent }) => {
            if (nativeEvent.actionName === "increment") commit(props.value + props.step);
            else if (nativeEvent.actionName === "decrement") commit(props.value - props.step);
          }}
          onKeyDown={({ nativeEvent }) => {
            const direction = KEY_STEPS[nativeEvent.key];
            if (direction !== undefined) commit(props.value + direction * props.step);
          }}
          onLayout={(event) => setWidth(event.nativeEvent.layout.width)}
          onStartShouldSetResponder={() => !props.disabled}
          onMoveShouldSetResponder={() => !props.disabled}
          onResponderGrant={(event) => setDraft(valueAt(event))}
          onResponderMove={(event) => setDraft(valueAt(event))}
          onResponderRelease={(event) => commit(valueAt(event))}
          onResponderTerminate={() => setDraft(null)}
          style={{ flex: 1, height: SLIDER_HEIGHT, justifyContent: "center" }}
        >
          <View
            pointerEvents="none"
            style={{
              marginHorizontal: THUMB_SIZE / 2,
              height: TRACK_HEIGHT,
              borderRadius: TRACK_HEIGHT / 2,
              overflow: "hidden",
            }}
          >
            <View style={[StyleSheet.absoluteFill, { backgroundColor: colors["--color-foreground"], opacity: TRACK_OPACITY }]} />
            <View
              style={{ width: `${fraction * 100}%`, height: "100%", backgroundColor: colors["--color-primary"] }}
            />
          </View>
          <View
            pointerEvents="none"
            style={{
              position: "absolute",
              left: fraction * trackWidth,
              width: THUMB_SIZE,
              height: THUMB_SIZE,
              borderRadius: THUMB_SIZE / 2,
              borderWidth: 1,
              borderColor: colors["--color-border"],
              backgroundColor: "#ffffff",
            }}
          />
        </View>
        {props.maximumIcon ? (
          <SymbolView name={props.maximumIcon} size={22} tintColorClassName="accent-icon-muted" />
        ) : null}
      </View>
    </View>
  );
}
