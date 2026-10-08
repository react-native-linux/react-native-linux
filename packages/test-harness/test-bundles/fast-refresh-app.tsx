import { Text } from "react-native";

// #81: The line Fast Refresh is graded on. Module scope runs again whenever an edit re-executes this module, so the
// trace names the title the running app now holds. The e2e scenario edits this exact string.
const title = "Before Fast Refresh";

console.log(`fast-refresh: title ${title}`);

export const FastRefreshApp = (): React.JSX.Element => <Text testID="fast-refresh">{title}</Text>;
