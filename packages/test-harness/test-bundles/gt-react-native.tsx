import { AppRegistry, Text, TurboModuleRegistry } from "react-native";
import { GTProvider, initializeGT, useGT, useLocale, useSetLocale } from "gt-react-native";
import type { TurboModule } from "react-native";
import structuredClone from "@ungap/structured-clone";
import { useEffect } from "react";

interface DevSettingsSpec extends TurboModule {
  readonly reload: () => void;
}

// #647: the scenario runs the window with LANG=de_DE.UTF-8. With nothing stored, GTProvider follows the system
// language through GtReactNative's getNativeLocales and renders German. The bundle then picks French, as T3 Code's
// language setting does, and reloads; the second instance starts in French from GtReactNative's store.
// The key is T3 Code's own catalog entry for gt("Settings").
const SETTINGS = "0a75e464d833bca8";
const TRANSLATIONS: Readonly<Record<string, Readonly<Record<string, string>>>> = {
  de: { [SETTINGS]: "Einstellungen" },
  fr: { [SETTINGS]: "Réglages" },
};

let isSwitching = false;

// gt-i18n clones its caches with the structuredClone global, which Hermes lacks; Expo's runtime installs this one
// (expo/src/winter/runtime.native.ts), which is where T3 Code gets it.
globalThis.structuredClone ??= structuredClone;

initializeGT({
  defaultLocale: "en",
  loadTranslations: async (locale: string) => TRANSLATIONS[locale] ?? {},
  locales: ["en", "de", "fr"],
});

const report = (line: string): void => {
  console.log(`gt-react-native: ${line}`);
};

const Settings = (): React.JSX.Element => {
  const gt = useGT();
  const locale = useLocale();
  const setLocale = useSetLocale();
  const title = gt("Settings");

  useEffect(() => {
    if (locale === "de") {
      report(`system ${locale} ${title}`);
      isSwitching = true;
      setLocale("fr");
    } else if (isSwitching) {
      report(`picked ${locale} ${title}`);
      TurboModuleRegistry.getEnforcing<DevSettingsSpec>("DevSettings").reload();
    } else {
      report(`restored ${locale} ${title}`);
    }
  }, [locale, setLocale, title]);

  return <Text>{title}</Text>;
};

const GtReactNativeApp = (): React.JSX.Element => (
  <GTProvider>
    <Settings />
  </GTProvider>
);

AppRegistry.registerComponent("GtReactNative", () => GtReactNativeApp);
AppRegistry.runApplication("GtReactNative", { initialProps: {}, rootTag: 1 });
