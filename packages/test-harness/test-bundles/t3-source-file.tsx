import { useEffect } from "react";
import { AppRegistry, FlatList, type NativeScrollEvent, type NativeSyntheticEvent, Text, View } from "react-native";

// #684: a 2,000-line highlighted file in the shape T3 Code's SourceFileSurface renders on every platform without its
// iOS review view: a virtualized list of HighlightedSourceLine rows, each a gutter Text beside a Text with one nested,
// colored Text per token. The sizes are T3 Code's MOBILE_CODE_SURFACE and the colors are pierre-dark's, from
// highlights.generated.ts. The default font keeps the golden reproducible until a monospace font is vendored (#156).

type TokenStyle = "bold" | "italic" | "normal";
type Token = readonly [content: string, color: string, style: TokenStyle];

const LINE_COUNT = 2000;
const ROW_HEIGHT = 22;
const GUTTER_WIDTH = 46;
const LIST_PADDING_TOP = 8;
// The rows the 720-pixel window shows, so a jump renders the whole viewport in one batch and the golden never
// catches a half-filled window. windowSize 3 is about T3 Code's drawDistance of 40 rows.
const VIEWPORT_ROWS = 34;
const FOREGROUND = "#fafafa";
const KEYWORD = "#ff678d";
const FUNCTION_NAME = "#9d6afb";
const TYPE_NAME = "#68cdf2";
const STRING = "#ffa359";
const NUMBER = "#ffd452";
const COMMENT = "#636363";

const lineTemplates: readonly ((line: number) => readonly Token[])[] = [
  (line) => [
    ["export function ", KEYWORD, "normal"],
    [`renderRow${String(line)}`, FUNCTION_NAME, "normal"],
    ["(props: ", FOREGROUND, "normal"],
    ["RowProps", TYPE_NAME, "normal"],
    [") {", FOREGROUND, "normal"],
  ],
  (line) => [
    ["  const ", KEYWORD, "normal"],
    ["label", FOREGROUND, "normal"],
    [" = ", FOREGROUND, "normal"],
    [`"row ${String(line)}"`, STRING, "normal"],
    [";", FOREGROUND, "normal"],
  ],
  (line) => [[`  // line ${String(line)} keeps its colors while it scrolls`, COMMENT, "italic"]],
  (line) => [
    ["  return ", KEYWORD, "normal"],
    ["<", FOREGROUND, "normal"],
    ["Text", TYPE_NAME, "bold"],
    [" size={", FOREGROUND, "normal"],
    [String(line % 97), NUMBER, "normal"],
    ["}>{label}</", FOREGROUND, "normal"],
    ["Text", TYPE_NAME, "bold"],
    [">;", FOREGROUND, "normal"],
  ],
  () => [["}", FOREGROUND, "normal"]],
  () => [],
];

const lineIndexes = Array.from({ length: LINE_COUNT }, (_, index) => index);

const tokensOf = (index: number): readonly Token[] =>
  lineTemplates[index % lineTemplates.length]?.(index + 1) ?? [];

const TokenText = ({ token: [content, color, style] }: { readonly token: Token }): React.JSX.Element => (
  <Text
    style={{
      color,
      fontStyle: style === "italic" ? "italic" : "normal",
      fontWeight: style === "bold" ? "700" : "400",
    }}
  >
    {content}
  </Text>
);

const SourceLine = ({ index }: { readonly index: number }): React.JSX.Element => {
  const tokens = tokensOf(index);
  let offset = 0;

  return (
    <View style={{ flexDirection: "row", height: ROW_HEIGHT }}>
      <Text
        style={{ color: "#737373", fontSize: 11, lineHeight: ROW_HEIGHT, paddingRight: 12, textAlign: "right", width: GUTTER_WIDTH }}
      >
        {index + 1}
      </Text>
      <Text numberOfLines={1} style={{ color: FOREGROUND, flex: 1, fontSize: 13, lineHeight: ROW_HEIGHT }}>
        {tokens.length === 0
          ? " "
          : tokens.map((token) => {
              const start = offset;
              offset += token[0].length;

              return <TokenText key={`${String(start)}:${token[0]}`} token={token} />;
            })}
      </Text>
    </View>
  );
};

const reportEnd = ({ nativeEvent }: NativeSyntheticEvent<NativeScrollEvent>): void => {
  if (nativeEvent.contentOffset.y + nativeEvent.layoutMeasurement.height >= nativeEvent.contentSize.height - 1) {
    console.log(`t3-source-file: scrolled to the end of ${String(LINE_COUNT)} lines`);
  }
};

const SourceFile = (): React.JSX.Element => {
  useEffect(() => {
    console.log("t3-source-file: committed");
  }, []);

  return (
    <View style={{ backgroundColor: "#0a0a0a", flex: 1 }}>
      <FlatList
        contentContainerStyle={{ paddingBottom: ROW_HEIGHT, paddingTop: LIST_PADDING_TOP }}
        data={lineIndexes}
        getItemLayout={(_, index) => ({ index, length: ROW_HEIGHT, offset: LIST_PADDING_TOP + ROW_HEIGHT * index })}
        keyExtractor={String}
        maxToRenderPerBatch={VIEWPORT_ROWS}
        onScroll={reportEnd}
        renderItem={({ item }) => <SourceLine index={item} />}
        scrollEventThrottle={16}
        windowSize={3}
      />
    </View>
  );
};

AppRegistry.registerComponent("T3SourceFile", () => SourceFile);
AppRegistry.runApplication("T3SourceFile", { initialProps: {}, rootTag: 1 });
