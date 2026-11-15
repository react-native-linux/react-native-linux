import { installExpoGlobalPolyfill } from "expo-modules-core/src/polyfill/dangerous-internal";

interface ExpoViewConfig {
  readonly directEventTypes: Record<string, never>;
  readonly validAttributes: Record<string, never>;
}

type HasComponent = (componentName: string) => boolean;

const isHasComponent = (value: unknown): value is HasComponent => typeof value === "function";

/**
 * The host component name `requireNativeView` registers for an Expo view, built as expo-modules-core's
 * `NativeViewManagerAdapter.native.tsx` builds it. The polyfill sets no `__expo_app_identifier__`, so no suffix.
 */
const expoViewComponentName = (moduleName: string, viewName = ""): string =>
  viewName === "" ? `ViewManagerAdapter_${moduleName}` : `ViewManagerAdapter_${moduleName}_${viewName}`;

/**
 * #662: the view config of an Expo view this platform renders, which is one with a Fabric component registered under
 * its host name, built in or autolinked. `FabricHost` binds upstream's `__nativeComponentRegistry__hasComponent` to
 * that registry. The view takes the base View attributes `createViewConfig` composes in; a view with props of its own
 * lists them here when the first one lands (#165). Anything else answers `null`, upstream's answer for a view that
 * is not linked: `requireNativeView` warns and the view mounts as an empty one.
 */
const getLinuxViewConfig = (moduleName: string, viewName?: string): ExpoViewConfig | null => {
  const hasComponent: unknown = Reflect.get(globalThis, "__nativeComponentRegistry__hasComponent");

  return isHasComponent(hasComponent) && hasComponent(expoViewComponentName(moduleName, viewName))
    ? { directEventTypes: {}, validAttributes: {} }
    : null;
};

installExpoGlobalPolyfill();
globalThis.expo.getViewConfig = getLinuxViewConfig;
