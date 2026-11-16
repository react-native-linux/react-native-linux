import { View } from "react-native";

import { AppText } from "./AppText";
import { clampNumber } from "./controlValues.linux";
import { IconButton } from "./IconButton";
import type { NumberFieldProps } from "./NumberField.types";
import { useControlTheme } from "./useControlTheme";

// react-native-linux: the Android stepper rather than iOS's wheel in a popover, which needs a
// SwiftUI Picker.
export function NumberField(props: NumberFieldProps) {
  const { colors } = useControlTheme();
  const adjust = (amount: number) => {
    if (props.disabled) return;
    const next = clampNumber(props.value + amount, props.min, props.max);
    if (next !== props.value) props.onValueChange(next);
  };

  return (
    <View style={{ flexShrink: 0, flexDirection: "row", alignItems: "center" }}>
      <View
        pointerEvents="none"
        style={{
          position: "absolute",
          left: 0,
          right: 0,
          height: 32,
          borderRadius: 16,
          backgroundColor: colors["--color-subtle"],
        }}
      />
      <IconButton
        icon="minus"
        accessibilityLabel={`Decrease ${props.label}`}
        disabled={props.disabled || props.value <= props.min}
        onPress={() => adjust(-1)}
      />
      <AppText
        textRole="body"
        style={{
          minWidth: 32,
          textAlign: "center",
          fontVariant: ["tabular-nums"],
          color: colors["--color-foreground"],
        }}
        accessibilityLabel={`${props.label}: ${props.formatValue?.(props.value) ?? props.value}`}
        accessibilityLiveRegion="polite"
      >
        {props.value}
      </AppText>
      <IconButton
        icon="plus"
        accessibilityLabel={`Increase ${props.label}`}
        disabled={props.disabled || props.value >= props.max}
        onPress={() => adjust(1)}
      />
    </View>
  );
}
