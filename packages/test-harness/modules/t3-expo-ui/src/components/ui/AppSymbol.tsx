import { View } from "react-native";
import { useControlTheme } from "./useControlTheme";

export function SymbolView(props: {
  readonly name: string;
  readonly size: number;
  readonly tintColorClassName: "accent-icon" | "accent-icon-muted";
}) {
  const { colors } = useControlTheme();
  return (
    <View
      style={{
        width: props.size,
        height: props.size,
        borderRadius: props.size / 4,
        backgroundColor:
          colors[props.tintColorClassName === "accent-icon" ? "--color-icon" : "--color-icon-muted"],
      }}
    />
  );
}
