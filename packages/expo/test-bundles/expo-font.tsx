import * as Font from "expo-font";
import { AppRegistry, Text, TurboModuleRegistry, View } from "react-native";
import type { TurboModule } from "react-native";
import { registerRootComponent } from "expo";
import { useEffect } from "react";

interface SourceCodeSpec extends TurboModule {
  readonly getConstants: () => { scriptURL: string };
}

const FONT_FAMILY = "Brand Italic";

// The scenario bundle is `build/e2e-bundles/expo-font/expo-font.js`, three directories below the repository root,
// and `NotoSans-Italic.ttf` is a vendored face no fontFamily of this window resolves to by that name.
const bundleUrl = TurboModuleRegistry.getEnforcing<SourceCodeSpec>("SourceCode").getConstants().scriptURL;
const repositoryRoot = bundleUrl.split("/").slice(0, -4).join("/");
const fontUri = `file://${repositoryRoot}/packages/core/fonts/NotoSans-Italic.ttf`;

const ExpoFontApp = (): React.JSX.Element => {
  const [loaded, error] = Font.useFonts({ [FONT_FAMILY]: fontUri });

  useEffect(() => {
    if (loaded || error !== null) {
      console.log(
        `expo-font: rendered loaded=${String(loaded)} error=${String(error)} isLoaded=${String(Font.isLoaded(FONT_FAMILY))} ` +
          `listed=${String(Font.getLoadedFonts().includes(FONT_FAMILY))}`,
      );
    }
  }, [loaded, error]);

  return (
    <View
      style={{ backgroundColor: "#ffffff", height: 120, left: 40, position: "absolute", top: 40, width: 320 }}
      testID="expo-font"
    >
      {loaded ? (
        <Text style={{ color: "#000000", fontFamily: FONT_FAMILY, fontSize: 32, left: 16, position: "absolute", top: 36 }}>
          Brand Italic
        </Text>
      ) : null}
    </View>
  );
};

registerRootComponent(ExpoFontApp);
AppRegistry.runApplication("main", { initialProps: {}, rootTag: 1 });
