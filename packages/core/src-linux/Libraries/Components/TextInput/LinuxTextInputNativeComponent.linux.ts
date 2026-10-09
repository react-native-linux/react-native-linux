import type { TextInputProps } from "react-native";
import { __INTERNAL_VIEW_CONFIG } from "react-native/Libraries/Components/TextInput/AndroidTextInputNativeComponent";
import { get as getNativeComponent } from "react-native/Libraries/NativeComponent/NativeComponentRegistry";

const LinuxTextInputNativeComponent = getNativeComponent<TextInputProps>("TextInput", () => ({
  ...__INTERNAL_VIEW_CONFIG,
  uiViewClassName: "TextInput",
}));

export default LinuxTextInputNativeComponent;
