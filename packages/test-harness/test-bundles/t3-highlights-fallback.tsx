import { AppRegistry, View } from "react-native";
import { NitroModules } from "./t3code-desktop-profile/react-native-nitro-modules";
import { useEffect } from "react";

// #767: the desktop profile's react-native-nitro-modules in rnl_window, which registers no T3Highlights. T3 Code's
// lib/highlights.ts asks hasHybridObject first and so takes its plain-text branch; createHybridObject is never reached
// there, and here it must fail with TurboModuleRegistry.getEnforcing's error rather than answer null.

const HIGHLIGHTS = "T3Highlights";

const report = (line: string): void => {
  console.log(`t3-highlights-fallback: ${line}`);
};

const createOutcome = (): string => {
  try {
    NitroModules.createHybridObject(HIGHLIGHTS);

    return "returned";
  } catch (error) {
    return `threw: ${error instanceof Error ? error.message : String(error)}`;
  }
};

const Lookup = (): React.JSX.Element => {
  useEffect(() => {
    report(`createHybridObject ${HIGHLIGHTS} ${createOutcome()}`);
    report(`hasHybridObject ${HIGHLIGHTS} ${String(NitroModules.hasHybridObject(HIGHLIGHTS))}`);
  }, []);

  return <View style={{ backgroundColor: "#0a0a0a", flex: 1 }} />;
};

AppRegistry.registerComponent("T3HighlightsFallback", () => Lookup);
AppRegistry.runApplication("T3HighlightsFallback", { initialProps: {}, rootTag: 1 });
