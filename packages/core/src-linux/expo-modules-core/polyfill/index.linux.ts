import { installExpoGlobalPolyfill } from "expo-modules-core/src/polyfill/dangerous-internal";

installExpoGlobalPolyfill();
globalThis.expo.getViewConfig = (): null => null;
