import { LogBox } from "react-native";

// An Expo app's babel-preset-expo inlines process.env.EXPO_OS, which the harness's Babel config does not, and
// expo-modules-core warns about it in a development bundle as it is imported, so this runs first.
LogBox.ignoreLogs(["The global process.env.EXPO_OS is not defined"]);
