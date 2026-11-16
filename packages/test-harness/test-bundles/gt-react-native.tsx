import { AppRegistry, TurboModuleRegistry, View } from "react-native";
import type { TurboModule } from "react-native";
import { useEffect } from "react";

interface GtReactNativeSpec extends TurboModule {
  readonly getNativeLocales: () => string[];
  readonly nativeStoreGet: (key: string) => string | null;
  readonly nativeStoreSet: (key: string, value: string) => void;
}

interface DevSettingsSpec extends TurboModule {
  readonly reload: () => void;
}

// #647: gt-react-native's own lookup, which throws on Linux without the module, then the locale T3 Code's language
// picker stores. The first instance stores it and reloads; the second reads it back from the application's file.
const LOCALE_KEY = "generaltranslation.locale";

const report = (line: string): void => {
  console.log(`gt-react-native: ${line}`);
};

const GtReactNativeApp = (): React.JSX.Element => {
  useEffect(() => {
    const gt = TurboModuleRegistry.getEnforcing<GtReactNativeSpec>("GtReactNative");
    const locales = gt.getNativeLocales();
    const stored = gt.nativeStoreGet(LOCALE_KEY);

    report(`locales ${Array.isArray(locales) && locales.every((tag) => typeof tag === "string") ? "tags" : "invalid"}`);

    if (stored === null) {
      gt.nativeStoreSet(LOCALE_KEY, "fr");
      report("stored fr");
      TurboModuleRegistry.getEnforcing<DevSettingsSpec>("DevSettings").reload();
    } else {
      report(`read ${stored}`);
    }
  }, []);

  return <View />;
};

AppRegistry.registerComponent("GtReactNative", () => GtReactNativeApp);
AppRegistry.runApplication("GtReactNative", { initialProps: {}, rootTag: 1 });
