import { useCallback, useEffect, useRef, useState } from "react";
import { AppRegistry, type NativeSyntheticEvent, View } from "react-native";
import {
  type NativeReviewDiffRow,
  type NativeReviewDiffToken,
  resolveNativeReviewDiffView,
} from "../modules/t3-review-diff/src/features/diffs/nativeReviewDiffSurface";

// #685: T3 Code's review sheet over a large diff. The rows, theme and style are the payloads ReviewSheet hands the
// native view (nativeReviewDiffAdapter's shapes, MOBILE_CODE_SURFACE sizes, the dark diff colors), and tokens arrive as
// the highlighter sends them: patches around each visible range, the next one only after the last is acknowledged.
// Below it, SourceFileSurface's use of the same view: a short file, contentWidth 32,000, mid pull-to-refresh.

type Token = readonly [content: string, color: string, fontStyle: number];
type Change = "add" | "context" | "delete";

interface VisibleRange {
  readonly first: number;
  readonly last: number;
}

const ROW_HEIGHT = 22;
const SOURCE_FILE_HEIGHT = 140;
const SOURCE_CONTENT_WIDTH = 32_000;
const LARGE_FILE_COUNT = 41;
const LARGE_FILE_HUNKS = 4;
const HUNK_SHAPE: readonly (readonly [Change, number])[] = [
  ["context", 3],
  ["delete", 6],
  ["add", 7],
  ["context", 5],
  ["delete", 3],
  ["add", 4],
  ["context", 3],
];
const FIRST_FILE_HUNKS: readonly (readonly (readonly [Change, number])[])[] = [
  [
    ["context", 2],
    ["delete", 2],
    ["add", 3],
    ["context", 3],
  ],
  [
    ["context", 1],
    ["delete", 1],
    ["add", 1],
    ["context", 1],
  ],
];
const OVERSCAN_ROWS = 160;
const MAX_PATCH_ROWS = 360;
const RESET_KEY = "review:turn-1";
const FOREGROUND = "#e5e5e5";
const KEYWORD = "#ff678d";
const FUNCTION_NAME = "#9d6afb";
const TYPE_NAME = "#68cdf2";
const STRING = "#ffa359";
const NUMBER = "#ffd452";
const COMMENT = "#737373";
const ITALIC = 1;
const BOLD = 2;

const theme = {
  addBackground: "#0d2f28",
  addBar: "#00cab1",
  addText: "#5ecc71",
  background: "#0f0f10",
  border: "#2a2a2e",
  deleteBackground: "#391415",
  deleteBar: "#ff2e3f",
  deleteText: "#ff6762",
  headerBackground: "#0f0f10",
  hunkBackground: "#1c1c20",
  hunkText: "#d4d4d8",
  mutedText: "#8a8a8f",
  text: FOREGROUND,
  warningBackground: "#3a2a05",
  warningText: "#fbbf24",
};

const style = {
  changeBarWidth: 4,
  codeFontSize: 12,
  codeFontWeight: "regular",
  codePadding: 7,
  fileHeaderFontSize: 11,
  fileHeaderHeight: 56,
  fileHeaderHorizontalPadding: 10,
  fileHeaderMetaFontSize: 10,
  fileHeaderSubtextFontSize: 11,
  gutterWidth: 46,
  hunkFontSize: 11,
  hunkFontWeight: "medium",
  lineNumberFontSize: 11,
  lineNumberFontWeight: "regular",
  rowHeight: ROW_HEIGHT,
};

const sourceStyle = { ...style, changeBarWidth: 0 };

const sourceLines: readonly (readonly Token[])[] = [
  [["import ", KEYWORD, 0], ["{ useState }", FOREGROUND, 0], [" from ", KEYWORD, 0], ['"react"', STRING, 0], [";", FOREGROUND, 0]],
  [],
  [["export function ", KEYWORD, 0], ["SourceFile", FUNCTION_NAME, 0], ["(): void {", FOREGROUND, 0]],
  [["  const ", KEYWORD, 0], ["[path]", FOREGROUND, 0], [" = ", FOREGROUND, 0], ["useState", FUNCTION_NAME, 0], ["(", FOREGROUND, 0], ['"README.md"', STRING, 0], [");", FOREGROUND, 0]],
  [["}", FOREGROUND, 0]],
];
const lineTokens = (file: number, line: number, change: Change): readonly Token[] => {
  const name = change === "add" ? "computeNext" : "compute";
  switch (line % 6) {
    case 0:
      return [
        ["export function ", KEYWORD, 0],
        [`handler${String(file)}_${String(line)}`, FUNCTION_NAME, 0],
        ["(event: ", FOREGROUND, 0],
        ["ReviewEvent", TYPE_NAME, BOLD],
        ["): void {", FOREGROUND, 0],
      ];
    case 1:
      return [
        ["  const ", KEYWORD, 0],
        [`value${String(line)}`, FOREGROUND, 0],
        [" = ", FOREGROUND, 0],
        [name, FUNCTION_NAME, 0],
        ["(input, ", FOREGROUND, 0],
        [String(line * (change === "add" ? 3 : 2)), NUMBER, 0],
        [");", FOREGROUND, 0],
      ];
    case 2:
      return [
        ["  // ", COMMENT, ITALIC],
        [change === "add" ? "keeps" : "kept", COMMENT, ITALIC],
        [
          ` the selection while the list re-renders: every row of file ${String(file)} reads its tokens from the latest patch, so a scroll never paints a stale color, and line ${String(line)} stays wider than the viewport to exercise horizontal content`,
          COMMENT,
          ITALIC,
        ],
      ];
    case 3:
      return [
        ["  return ", KEYWORD, 0],
        ["<", FOREGROUND, 0],
        ["DiffRow", TYPE_NAME, BOLD],
        [" label={", FOREGROUND, 0],
        [`"${change === "add" ? "row" : "line"} ${String(line)}"`, STRING, 0],
        ["} />;", FOREGROUND, 0],
      ];
    case 4:
      return [
        ["import ", KEYWORD, 0],
        [`{ helper${String(line)} }`, FOREGROUND, 0],
        [" from ", KEYWORD, 0],
        [`"./module${String(file)}"`, STRING, 0],
        [";", FOREGROUND, 0],
      ];
    default:
      return [["}", FOREGROUND, 0]];
  }
};

const textOf = (tokens: readonly Token[]): string => tokens.map(([content]) => content).join("");

const sourceRowsJson = JSON.stringify(
  sourceLines.map((tokens, index) => ({
    change: "context",
    content: textOf(tokens),
    fileId: "source-file",
    id: `source-line:${String(index)}`,
    kind: "line",
    newLineNumber: index + 1,
  })),
);


// T3 Code drops a word diff that covers most of the line (NATIVE_REVIEW_MAX_WORD_DIFF_COVERAGE).
const MAX_WORD_DIFF_COVERAGE = 0.45;

const wordDiffRanges = (own: readonly Token[], other: readonly Token[]): { end: number; start: number }[] => {
  let offset = 0;
  let start = -1;
  let end = 0;
  own.forEach(([content], index) => {
    if (content !== other[index]?.[0]) {
      start = start < 0 ? offset : start;
      end = offset + content.length;
    }
    offset += content.length;
  });
  return start < 0 || end - start > offset * MAX_WORD_DIFF_COVERAGE ? [] : [{ end, start }];
};

const tokensByRowId = new Map<string, readonly Token[]>();

const fileRows = (file: number, hunks: readonly (readonly (readonly [Change, number])[])[]): NativeReviewDiffRow[] => {
  const fileId = `file-${String(file)}`;
  const lines: NativeReviewDiffRow[] = [];
  let additions = 0;
  let deletions = 0;
  hunks.forEach((shape, hunk) => {
    let oldLine = hunk * 60 + 1;
    let newLine = hunk * 62 + 1;
    const oldCount = shape.reduce((sum, [change, count]) => sum + (change === "add" ? 0 : count), 0);
    const newCount = shape.reduce((sum, [change, count]) => sum + (change === "delete" ? 0 : count), 0);
    lines.push({
      fileId,
      id: `${fileId}:hunk:${String(hunk)}`,
      kind: "hunk",
      text: `@@ -${String(oldLine)},${String(oldCount)} +${String(newLine)},${String(newCount)} @@ function handler${String(file)}`,
    });
    for (const [change, count] of shape) {
      for (let step = 0; step < count; step += 1) {
        const line = change === "delete" ? oldLine : newLine;
        const tokens = lineTokens(file, line, change);
        const content = textOf(tokens);
        const id = `${fileId}:line:${String(lines.length)}`;
        const ranges = change === "context" ? [] : wordDiffRanges(tokens, lineTokens(file, line, change === "add" ? "delete" : "add"));
        tokensByRowId.set(id, tokens);
        lines.push({
          change,
          content,
          fileId,
          id,
          kind: "line",
          newLineNumber: change === "delete" ? null : newLine,
          oldLineNumber: change === "add" ? null : oldLine,
          ...(ranges.length === 0 ? {} : { wordDiffRanges: ranges }),
        });
        additions += change === "add" ? 1 : 0;
        deletions += change === "delete" ? 1 : 0;
        oldLine += change === "add" ? 0 : 1;
        newLine += change === "delete" ? 0 : 1;
      }
    }
  });
  return [
    {
      additions,
      changeType: file === 1 ? "renamed" : "modified",
      deletions,
      fileId,
      filePath: `apps/mobile/src/features/review/module${String(file)}/ReviewPane${String(file)}.tsx`,
      id: `${fileId}:header`,
      kind: "file",
      previousPath: file === 1 ? "apps/mobile/src/features/review/module1/ReviewList.tsx" : null,
    },
    ...lines,
  ];
};

const rows: readonly NativeReviewDiffRow[] = [
  ...fileRows(0, FIRST_FILE_HUNKS),
  { fileId: "file-0", id: "file-0:notice:eof", kind: "notice", text: "New file has no newline at end of file." },
  ...Array.from({ length: LARGE_FILE_COUNT }, (_, index) =>
    fileRows(index + 1, Array.from({ length: LARGE_FILE_HUNKS }, () => HUNK_SHAPE)),
  ).flat(),
];
const rowsJson = JSON.stringify(rows);
const lineCount = rows.filter((row) => row.kind === "line").length;

const report = (line: string): void => {
  console.log(`t3-review-diff: ${line}`);
};

const nativeTokens = (tokens: readonly Token[]): NativeReviewDiffToken[] =>
  tokens.map(([content, color, fontStyle]) => ({ color, content, fontStyle }));

const sourceTokensJson = JSON.stringify(
  Object.fromEntries(sourceLines.map((tokens, index) => [`source-line:${String(index)}`, nativeTokens(tokens)])),
);

const patchFor = (range: VisibleRange, sent: Set<string>): Record<string, NativeReviewDiffToken[]> => {
  const patch: Record<string, NativeReviewDiffToken[]> = {};
  let count = 0;
  for (let index = Math.max(0, range.first - OVERSCAN_ROWS); index <= range.last + OVERSCAN_ROWS; index += 1) {
    const id = rows[index]?.id ?? "";
    const tokens = tokensByRowId.get(id);
    if (tokens !== undefined && !sent.has(id) && count < MAX_PATCH_ROWS) {
      sent.add(id);
      patch[id] = nativeTokens(tokens);
      count += 1;
    }
  }
  return patch;
};

const eventPayload = (event: NativeSyntheticEvent<Record<string, unknown>>): Record<string, unknown> => event.nativeEvent;

const ReviewDiffView = resolveNativeReviewDiffView();

const ReviewSheet = (): React.JSX.Element => {
  const [collapsedFileIds, setCollapsedFileIds] = useState<readonly string[]>(["file-1"]);
  const [tokensPatchJson, setTokensPatchJson] = useState<string>();
  const sent = useRef(new Set<string>());
  const wanted = useRef<VisibleRange | null>(null);
  const awaiting = useRef(false);
  const chunkIndex = useRef(0);
  const reachedEnd = useRef(false);

  const pump = useCallback(() => {
    if (awaiting.current || wanted.current === null) {
      return;
    }
    const patch = patchFor(wanted.current, sent.current);
    if (Object.keys(patch).length === 0) {
      return;
    }
    awaiting.current = true;
    setTokensPatchJson(JSON.stringify({ chunkIndex: chunkIndex.current, resetKey: RESET_KEY, tokensByRowId: patch }));
    chunkIndex.current += 1;
  }, []);

  const onDebug = useCallback(
    (event: NativeSyntheticEvent<Record<string, unknown>>) => {
      const { chunkIndex: deliveredChunk, firstRowIndex, lastRowIndex, message, totalRows } = eventPayload(event);
      if (message === "visible-range" && typeof firstRowIndex === "number" && typeof lastRowIndex === "number") {
        wanted.current = { first: firstRowIndex, last: lastRowIndex };
        if (!reachedEnd.current && lastRowIndex === rows.length - 1) {
          reachedEnd.current = true;
          report(`reached row ${String(lastRowIndex + 1)} of ${String(totalRows)}`);
        }
        pump();
      }
      if (message === "tokens-patch-delivered") {
        awaiting.current = false;
        if (deliveredChunk === 0) {
          report("tokens chunk 0 delivered");
        }
        pump();
      }
    },
    [pump],
  );

  useEffect(() => {
    report(`committed ${String(LARGE_FILE_COUNT + 1)} files, ${String(lineCount)} lines, ${String(rows.length)} rows`);
  }, []);

  return (
    <View style={{ backgroundColor: theme.background, flex: 1 }}>
      <ReviewDiffView
        appearanceScheme="dark"
        collapsedFileIdsJson={JSON.stringify(collapsedFileIds)}
        contentResetKey={RESET_KEY}
        contentWidth={2800}
        onDebug={onDebug}
        onPressLine={({ nativeEvent }) => {
          report(
            `pressLine ${nativeEvent.gesture ?? ""} row=${nativeEvent.rowId ?? ""} change=${nativeEvent.change ?? ""} new=${String(nativeEvent.newLineNumber)}`,
          );
        }}
        onToggleFile={({ nativeEvent: { fileId = "" } }) => {
          report(`toggleFile ${fileId}`);
          setCollapsedFileIds((current) =>
            current.includes(fileId) ? current.filter((id) => id !== fileId) : [...current, fileId],
          );
        }}
        onVisibleFileChange={({ nativeEvent: { fileId } }) => {
          report(`visible file ${fileId ?? ""}`);
        }}
        rowHeight={ROW_HEIGHT}
        rowsJson={rowsJson}
        style={{ flex: 1 }}
        styleJson={JSON.stringify(style)}
        themeJson={JSON.stringify(theme)}
        tokensPatchJson={tokensPatchJson}
        tokensResetKey={RESET_KEY}
      />
      <ReviewDiffView
        appearanceScheme="dark"
        contentResetKey="source:README.md"
        contentWidth={SOURCE_CONTENT_WIDTH}
        refreshing
        rowHeight={ROW_HEIGHT}
        rowsJson={sourceRowsJson}
        style={{ borderColor: theme.border, borderTopWidth: 1, height: SOURCE_FILE_HEIGHT }}
        styleJson={JSON.stringify(sourceStyle)}
        themeJson={JSON.stringify(theme)}
        tokensJson={sourceTokensJson}
      />
    </View>
  );
};

AppRegistry.registerComponent("T3ReviewDiff", () => ReviewSheet);
AppRegistry.runApplication("T3ReviewDiff", { initialProps: {}, rootTag: 1 });
