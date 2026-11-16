import { type TurboModule, TurboModuleRegistry } from "react-native";

/**
 * `react-native-nitro-modules` until Nitro runs on Linux (#152): 0.37.2 throws as it loads. T3 Code's
 * `lib/highlights.ts`, its one importer, looks up `T3Highlights` here, which resolves through `TurboModuleRegistry`,
 * where #684's Linux module registers; without it, code renders as plain text, T3 Code's own fallback.
 */
const NitroModules = {
  createHybridObject: <HybridObject extends TurboModule>(name: string): HybridObject =>
    TurboModuleRegistry.getEnforcing<HybridObject>(name),
  hasHybridObject: (name: string): boolean => TurboModuleRegistry.get(name) !== null,
};

export { NitroModules };
