import { useState } from "react";
import { AppRegistry, View } from "react-native";
import { NumberField } from "../modules/t3-expo-ui/src/components/ui/NumberField.linux";
import { Progress } from "../modules/t3-expo-ui/src/components/ui/Progress.linux";
import { SliderRow } from "../modules/t3-expo-ui/src/components/ui/SliderRow.linux";
import { ControlColorScheme, useControlTheme } from "../modules/t3-expo-ui/src/components/ui/useControlTheme";

const COLUMN_WIDTH = 400;
const GAP = 16;
const PADDING = 8;
const INITIAL_FONT_SIZE = 16;
const INITIAL_AUTO_SETTLE_DAYS = 7;

const report = (line: string): void => {
  console.log(`t3-expo-ui: ${line}`);
};

const Controls = ({ name }: { readonly name: string }): React.JSX.Element => {
  const { colors } = useControlTheme();
  const [fontSize, setFontSize] = useState(INITIAL_FONT_SIZE);
  const [days, setDays] = useState(INITIAL_AUTO_SETTLE_DAYS);

  return (
    <View style={{ backgroundColor: colors["--color-card"], gap: PADDING, padding: PADDING, width: COLUMN_WIDTH }}>
      <Progress label="Downloading" value={0.35} />
      <Progress label="Downloaded" value={1} />
      <SliderRow
        icon="textformat.size"
        label="Font size"
        max={24}
        maximumIcon="textformat.size.larger"
        min={12}
        minimumIcon="textformat.size.smaller"
        onChange={(value) => {
          report(`${name} slider value=${String(value)}`);
          setFontSize(value);
        }}
        step={1}
        value={fontSize}
        valueLabel={`${String(fontSize)} pt`}
      />
      <NumberField
        label="Auto settle"
        max={30}
        min={1}
        onValueChange={(value) => {
          report(`${name} number value=${String(value)}`);
          setDays(value);
        }}
        value={days}
      />
      <SliderRow
        disabled
        icon="speaker.wave.2"
        label="Volume"
        max={1}
        min={0}
        onChange={() => {
          report(`${name} disabled slider changed`);
        }}
        step={0.1}
        value={0.5}
        valueLabel="50%"
      />
      <NumberField
        label="Retries"
        max={3}
        min={1}
        onValueChange={() => {
          report(`${name} number at its minimum changed`);
        }}
        value={1}
      />
    </View>
  );
};

const T3ExpoUi = (): React.JSX.Element => (
  <View style={{ backgroundColor: "#808080", flex: 1, flexDirection: "row", gap: GAP, padding: GAP }}>
    <ControlColorScheme value="light">
      <Controls name="light" />
    </ControlColorScheme>
    <ControlColorScheme value="dark">
      <Controls name="dark" />
    </ControlColorScheme>
  </View>
);

AppRegistry.registerComponent("T3ExpoUi", () => T3ExpoUi);
AppRegistry.runApplication("T3ExpoUi", { initialProps: {}, rootTag: 1 });
report("ready");
