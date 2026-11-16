/**
 * The desktop feature profile's `@expo/ui/community/datetime-picker` (docs/research/t3code-linux.md §2): upstream's
 * picker is a SwiftUI or Compose view, and its module requires the declined `ExpoUI` native module as it loads, so on
 * Linux the picker draws nothing and says so once on the console, which the e2e event trace records.
 */
const state = { hasWarned: false };

const DateTimePicker = (): null => {
  if (!state.hasWarned) {
    state.hasWarned = true;
    console.warn("[@expo/ui/community/datetime-picker] DateTimePicker is not drawn on Linux (#750)");
  }

  return null;
};

export { DateTimePicker };
