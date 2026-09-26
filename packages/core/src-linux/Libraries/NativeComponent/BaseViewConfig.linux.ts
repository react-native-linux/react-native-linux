import BaseViewConfigAndroid from "react-native/Libraries/NativeComponent/BaseViewConfig.android";
import BaseViewConfigIos from "react-native/Libraries/NativeComponent/BaseViewConfig.ios";
import { mergeBaseViewConfigs } from "./mergeBaseViewConfigs.ts";

const BaseViewConfig: ReturnType<typeof mergeBaseViewConfigs> = mergeBaseViewConfigs(
  BaseViewConfigAndroid,
  BaseViewConfigIos,
);

// React Native's own modules import this file as `import BaseViewConfig from './BaseViewConfig'` (#22).
export default BaseViewConfig;
