import { useState } from "react";
import { AppRegistry, View } from "react-native";
import { CustomSnoozeSheet } from "../modules/t3-expo-ui/src/features/threads/CustomSnoozeSheet.linux";
import { DateTimePicker } from "./t3code-desktop-profile/expo-ui-datetime-picker";
import { Progress } from "../modules/t3-expo-ui/src/components/ui/Progress.linux";
import { SliderRow } from "../modules/t3-expo-ui/src/components/ui/SliderRow.linux";
import { ControlColorScheme, useControlTheme } from "../modules/t3-expo-ui/src/components/ui/useControlTheme";

const COLUMN_WIDTH = 400;
const GAP = 16;
const PADDING = 8;
const SHEET_HEIGHT = 300;
const INITIAL_FONT_SIZE = 16;
const PICKER_YEAR = 2026;
const PICKER_HOUR = 9;
const PART_LENGTH = 2;
const MONTH_OFFSET = 1;

const report = (line: string): void => {
  console.log(`t3-expo-ui: ${line}`);
};

const pad = (part: number): string => String(part).padStart(PART_LENGTH, "0");

const Controls = ({ name }: { readonly name: string }): React.JSX.Element => {
  const { colors } = useControlTheme();
  const [fontSize, setFontSize] = useState(INITIAL_FONT_SIZE);

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
    </View>
  );
};

const Sheets = (): React.JSX.Element => {
  const { colors } = useControlTheme();
  const [isSnoozed, setIsSnoozed] = useState(false);
  const [pickerValue] = useState(() => new Date(PICKER_YEAR, 0, 1, PICKER_HOUR, 0));

  return (
    <View style={{ backgroundColor: colors["--color-card"], gap: PADDING, padding: PADDING, width: COLUMN_WIDTH }}>
      <View style={{ height: SHEET_HEIGHT }}>
        <CustomSnoozeSheet
          onClose={() => {
            report("snooze sheet closed");
          }}
          onSnooze={(snoozedUntil) => {
            report(`snoozed ${snoozedUntil}`);
            setIsSnoozed(true);
          }}
        />
      </View>
      <DateTimePicker
        mode="time"
        onValueChange={(_event, date) => {
          report(`time ${pad(date.getHours())}:${pad(date.getMinutes())}`);
        }}
        value={pickerValue}
      />
      <DateTimePicker
        mode="date"
        onValueChange={(_event, date) => {
          report(`date ${String(date.getFullYear())}-${pad(date.getMonth() + MONTH_OFFSET)}-${pad(date.getDate())}`);
        }}
        value={pickerValue}
      />
      {isSnoozed ? null : (
        <View style={{ marginTop: "auto" }}>
          <Progress label="Connecting" />
        </View>
      )}
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
    <ControlColorScheme value="light">
      <Sheets />
    </ControlColorScheme>
  </View>
);

AppRegistry.registerComponent("T3ExpoUi", () => T3ExpoUi);
AppRegistry.runApplication("T3ExpoUi", { initialProps: {}, rootTag: 1 });
report("ready");
