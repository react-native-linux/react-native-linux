import { AppRegistry, ScrollView, Text, View } from "react-native";
import { NitroModules } from "./t3code-desktop-profile/react-native-nitro-modules";
import { useEffect } from "react";

// #767: T3 Code's lib/highlights.ts looks T3Highlights up through the desktop profile's react-native-nitro-modules.
// rnl_window registers no T3Highlights, so a fenced block takes T3 Code's plain-text branch: one uncolored token per
// line, CR and LF dropped, inside HighlightedCodeBlock's Text, whose code color every token inherits. The frame is
// CodeBlockFrame's border and label; the colors are T3 Code's dark markdown theme.

const CODE = 'const greeting = "hi, é";\r\n\nexport default greeting;\n';
const CODE_COLOR = "#fafafa";

const hasHighlights = NitroModules.hasHybridObject("T3Highlights");
const lines = CODE.split(/\r?\n/u).map((content) => (content === "" ? [] : [content]));

const CodeBlock = (): React.JSX.Element => {
  useEffect(() => {
    console.log(`t3-highlights-fallback: T3Highlights ${hasHighlights ? "present" : "absent"}, ${String(lines.length)} plain lines`);
  }, []);

  return (
    <View style={{ backgroundColor: "#0a0a0a", flex: 1, padding: 16 }}>
      <View style={{ backgroundColor: "#171717", borderColor: "#262626", borderRadius: 8, borderWidth: 1 }}>
        <View style={{ borderBottomColor: "#fafafa33", borderBottomWidth: 1, paddingHorizontal: 12, paddingVertical: 6 }}>
          <Text style={{ color: "#fafafa99", fontSize: 11 }}>Tsx</Text>
        </View>
        <ScrollView horizontal contentContainerStyle={{ flexGrow: 1 }}>
          <Text style={{ color: CODE_COLOR, fontSize: 13, lineHeight: 20, padding: 12 }}>
            {lines.flatMap((line, lineIndex) => [
              lineIndex > 0 ? "\n" : "",
              ...line.map((content) => <Text key={lineIndex}>{content}</Text>),
            ])}
          </Text>
        </ScrollView>
      </View>
    </View>
  );
};

AppRegistry.registerComponent("T3HighlightsFallback", () => CodeBlock);
AppRegistry.runApplication("T3HighlightsFallback", { initialProps: {}, rootTag: 1 });
