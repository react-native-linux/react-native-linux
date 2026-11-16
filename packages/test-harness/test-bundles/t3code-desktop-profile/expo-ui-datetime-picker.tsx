import { TextInput, useColorScheme } from "react-native";

const MONTH_OFFSET = 1;
const PART_LENGTH = 2;

type Field = "date" | "time";

interface DateTimePickerProps {
  readonly mode?: Field | "datetime";
  readonly onValueChange?: (
    event: { readonly nativeEvent: { readonly timestamp: number; readonly utcOffset: number } },
    date: Date,
  ) => void;
  readonly value: Date;
}

const pad = (part: number): string => String(part).padStart(PART_LENGTH, "0");

const formatField = (field: Field, date: Date): string =>
  field === "date"
    ? `${String(date.getFullYear())}-${pad(date.getMonth() + MONTH_OFFSET)}-${pad(date.getDate())}`
    : `${pad(date.getHours())}:${pad(date.getMinutes())}`;

/** `value` with the typed field set, or null until the text names a real date or time and so formats back to itself. */
const applyTyped = (field: Field, value: Date, text: string): Date | null => {
  const [first = Number.NaN, second = Number.NaN, third = Number.NaN] = text.split(/[-:]/u).map(Number);
  const next = new Date(value);

  if (field === "date") {
    next.setFullYear(first, second - MONTH_OFFSET, third);
  } else {
    next.setHours(first, second);
  }

  return formatField(field, next) === text ? next : null;
};

/**
 * The desktop feature profile's `@expo/ui/community/datetime-picker` (docs/research/t3code-linux.md §2): upstream's
 * picker is a SwiftUI or Compose view, and its module requires the declined `ExpoUI` native module as it loads. Until
 * a real picker lands (#750) the date is typed as YYYY-MM-DD and the time as HH:MM, reported once the text parses.
 */
const DateTimePicker = (props: DateTimePickerProps): React.JSX.Element => {
  const field: Field = props.mode === "time" ? "time" : "date";
  const color = useColorScheme() === "dark" ? "#f5f5f5" : "#27272a";

  return (
    <TextInput
      defaultValue={formatField(field, props.value)}
      accessibilityLabel={field === "date" ? "Date, YYYY-MM-DD" : "Time, HH:MM"}
      onChangeText={(nextText) => {
        const date = applyTyped(field, props.value, nextText);

        if (date !== null) {
          props.onValueChange?.(
            { nativeEvent: { timestamp: date.getTime(), utcOffset: -date.getTimezoneOffset() } },
            date,
          );
        }
      }}
      placeholder={field === "date" ? "YYYY-MM-DD" : "HH:MM"}
      style={{ borderColor: color, borderRadius: 8, borderWidth: 1, color, fontSize: 16, height: 40, paddingHorizontal: 12 }}
    />
  );
};

export { DateTimePicker };
