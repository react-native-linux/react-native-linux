import { Pressable } from "react-native";
import { AppText } from "./AppText";
import { useControlTheme } from "./useControlTheme";

const glyphs = { minus: "−", plus: "+" } as const;

export function IconButton(props: {
  readonly accessibilityLabel: string;
  readonly icon: keyof typeof glyphs;
  readonly disabled: boolean;
  readonly onPress: () => void;
}) {
  const { colors } = useControlTheme();
  return (
    <Pressable
      accessibilityLabel={props.accessibilityLabel}
      accessibilityRole="button"
      disabled={props.disabled}
      onPress={props.onPress}
      style={{ width: 48, height: 48, alignItems: "center", justifyContent: "center" }}
    >
      <AppText
        textRole="title"
        style={{ color: colors[props.disabled ? "--color-icon-muted" : "--color-icon"] }}
      >
        {glyphs[props.icon]}
      </AppText>
    </Pressable>
  );
}
