import { View } from "react-native";
import { useControlTheme } from "./useControlTheme";

export function SymbolView(props: {
  readonly name: string;
  readonly size: number;
  readonly tintColor?: string;
  readonly tintColorClassName?: "accent-icon" | "accent-icon-muted";
  readonly type?: "monochrome";
}) {
  const { colors } = useControlTheme();
  return (
    <View
      style={{
        width: props.size,
        height: props.size,
        borderRadius: props.size / 4,
        backgroundColor:
          props.tintColor ??
          colors[props.tintColorClassName === "accent-icon" ? "--color-icon" : "--color-icon-muted"],
      }}
    />
  );
}
