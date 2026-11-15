import type { NativeReviewDiffRow, NativeReviewDiffViewProps } from "./src/features/diffs/nativeReviewDiffSurface.ts";
import {
  codeRuns,
  contentWidthOf,
  layoutOf,
  parseIds,
  readTokenPatch,
  visibleRowsOf,
} from "./src/features/diffs/nativeReviewDiffRows.ts";
import { describe, expect, it } from "vitest";

const ROW_HEIGHT = 20;
const TALL_ROW_HEIGHT = 30;
const TOP_INSET = 8;
const HEADER_HEIGHT = 56;
const NOTICE_HEIGHT = 44;
const COLLAPSED_COMMENT_HEIGHT = 44;
const EXPANDED_COMMENT_HEIGHT = 124;
const CODE_FONT_SIZE = 14;
const GUTTER_WIDTH = 52;
const HUNK_FONT_SIZE = 20;
const DEJAVU_SANS_MONO_ADVANCE = 1233;
const DEJAVU_SANS_MONO_UNITS_PER_EM = 2048;
const MONOSPACE_ADVANCE_PER_EM = DEJAVU_SANS_MONO_ADVANCE / DEJAVU_SANS_MONO_UNITS_PER_EM;
const SOURCE_FILE_CONTENT_WIDTH = 32_000;
const REVIEW_CONTENT_WIDTH = 2800;
const BOLD = 2;
const CHUNK = 3;
const FIRST_CHUNK = 0;

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
  text: "#e5e5e5",
  warningBackground: "#3a2a05",
  warningText: "#fbbf24",
};

const layout = layoutOf({}, ROW_HEIGHT);
const unhighlighted: Readonly<Record<string, never[]>> = {};
const withoutCollapsedFiles: Pick<NativeReviewDiffViewProps, "collapsedFileIdsJson"> = {};

const rows: readonly NativeReviewDiffRow[] = [
  { fileId: "a", id: "a:header", kind: "file" },
  { fileId: "a", id: "a:hunk", kind: "hunk" },
  { fileId: "a", id: "a:line", kind: "line" },
  { fileId: "a", id: "a:comment:open", kind: "comment" },
  { fileId: "a", id: "a:comment:shut", kind: "comment" },
  { fileId: "a", id: "a:notice", kind: "notice" },
  { fileId: "b", id: "b:header", kind: "file" },
  { fileId: "b", id: "b:line", kind: "line" },
  { id: "c:header", kind: "file" },
  { id: "c:line", kind: "line" },
];

const describeRuns = (runs: ReturnType<typeof codeRuns>): string[] =>
  runs.map(({ background, color, content, fontStyle }) =>
    [content, color, background ?? "none", String(fontStyle)].join(" | "),
  );

const rangeOf = (content: string, word: string): { end: number; start: number } => ({
  end: content.indexOf(word) + word.length,
  start: content.indexOf(word),
});

describe("parseIds and layoutOf", () => {
  it("reads a JSON id list, and nothing when the prop is absent", () => {
    expect([...parseIds('["a","b"]')]).toStrictEqual(["a", "b"]);
    expect(parseIds(withoutCollapsedFiles.collapsedFileIdsJson).size).toBe(FIRST_CHUNK);
  });

  it("refuses JSON that is not an array of id strings, rather than reading a string as its characters", () => {
    expect(() => parseIds('"ab"')).toThrow('expected a JSON array of ids, got "ab"');
    expect(() => parseIds('["a",1]')).toThrow("expected a JSON array of ids");
  });

  it("takes the style's sizes and falls back to the mobile code surface's", () => {
    expect(layoutOf({ codeFontSize: CODE_FONT_SIZE, gutterWidth: GUTTER_WIDTH }, ROW_HEIGHT)).toStrictEqual({
      ...layout,
      codeFontSize: CODE_FONT_SIZE,
      gutterWidth: GUTTER_WIDTH,
    });
    expect(layout.fileHeaderHeight).toBe(HEADER_HEIGHT);
  });
});

describe("layoutOf font weights", () => {
  it("maps T3ReviewDiffView's weight names to CSS weights, and anything else to bold as UIFont does", () => {
    const weights = layoutOf(
      { codeFontWeight: "Regular", hunkFontWeight: "semi-bold", lineNumberFontWeight: "ultralight" },
      ROW_HEIGHT,
    );

    expect([weights.codeFontWeight, weights.hunkFontWeight, weights.lineNumberFontWeight]).toStrictEqual([
      "400",
      "600",
      "200",
    ]);
    expect([layout.codeFontWeight, layoutOf({ codeFontWeight: "condensed" }, ROW_HEIGHT).codeFontWeight]).toStrictEqual(
      ["700", "700"],
    );
  });
});

describe("contentWidthOf", () => {
  const chrome = layout.changeBarWidth + layout.gutterWidth + layout.codePadding + layout.codePadding;
  const hunkLayout = layoutOf({ codeFontSize: CODE_FONT_SIZE, hunkFontSize: HUNK_FONT_SIZE }, ROW_HEIGHT);
  const widthOf = (text: string, fontSize: number): number =>
    chrome + Math.ceil(text.length * fontSize * MONOSPACE_ADVANCE_PER_EM);

  it("measures the longest line in the code font and adds the chrome, as contentWidthsByFileId does", () => {
    const shortRows: readonly NativeReviewDiffRow[] = [
      { content: "abc", id: "l1", kind: "line" },
      { content: "abcdefghij", id: "l2", kind: "line" },
      { id: "l3", kind: "line" },
      { id: "h", kind: "file" },
    ];

    expect(contentWidthOf(shortRows, layout, SOURCE_FILE_CONTENT_WIDTH)).toBe(
      widthOf("abcdefghij", layout.codeFontSize),
    );
  });

  it("measures a hunk header, from its text or its content, in the hunk font", () => {
    expect(contentWidthOf([{ id: "h", kind: "hunk", text: "@@ -1 +1 @@" }], hunkLayout, REVIEW_CONTENT_WIDTH)).toBe(
      widthOf("@@ -1 +1 @@", HUNK_FONT_SIZE),
    );
    expect(contentWidthOf([{ content: "@@ x", id: "h", kind: "hunk" }], hunkLayout, REVIEW_CONTENT_WIDTH)).toBe(
      widthOf("@@ x", HUNK_FONT_SIZE),
    );
    expect(contentWidthOf([{ id: "h", kind: "hunk" }], hunkLayout, REVIEW_CONTENT_WIDTH)).toBe(chrome);
  });

  it("never exceeds the contentWidth the caller passes", () => {
    const longLine = "x".repeat(REVIEW_CONTENT_WIDTH);

    expect(contentWidthOf([{ content: longLine, id: "l", kind: "line" }], layout, REVIEW_CONTENT_WIDTH)).toBe(
      REVIEW_CONTENT_WIDTH,
    );
  });
});

describe("visibleRowsOf", () => {
  it("hides a collapsed file's rows and keeps each shown row's index in the full payload", () => {
    const visible = visibleRowsOf(rows, new Set(["b"]), new Set(["a:comment:shut"]), layout, TOP_INSET);

    expect(
      visible.indexes.map((index) => [rows[index]?.id, visible.heights[visible.indexes.indexOf(index)]]),
    ).toStrictEqual([
      ["a:header", HEADER_HEIGHT],
      ["a:hunk", ROW_HEIGHT],
      ["a:line", ROW_HEIGHT],
      ["a:comment:open", EXPANDED_COMMENT_HEIGHT],
      ["a:comment:shut", COLLAPSED_COMMENT_HEIGHT],
      ["a:notice", NOTICE_HEIGHT],
      ["b:header", HEADER_HEIGHT],
      ["c:header", HEADER_HEIGHT],
      ["c:line", ROW_HEIGHT],
    ]);
    expect(visible.offsets).toStrictEqual(
      visible.heights.map((_height, position) =>
        visible.heights.slice(FIRST_CHUNK, position).reduce((sum, height) => sum + height, TOP_INSET),
      ),
    );
  });

  it("gives a notice two rows once two rows are taller than the native minimum", () => {
    const notices = rows.filter((row) => row.kind === "notice");

    expect(
      visibleRowsOf(notices, new Set(), new Set(), layoutOf({}, TALL_ROW_HEIGHT), TOP_INSET).heights,
    ).toStrictEqual([TALL_ROW_HEIGHT + TALL_ROW_HEIGHT]);
  });
});

describe("codeRuns", () => {
  it("draws an untokenized line as one run of its content in the text color", () => {
    expect(
      describeRuns(codeRuns({ change: "context", content: "plain", id: "l", kind: "line" }, [], theme)),
    ).toStrictEqual([`plain | ${theme.text} | none | 0`]);
    expect(
      describeRuns(codeRuns({ change: "add", content: "new", id: "l", kind: "line" }, unhighlighted["l"], theme)),
    ).toStrictEqual([`new | ${theme.text} | none | 0`]);
    expect(codeRuns({ id: "l", kind: "line" }, unhighlighted["l"], theme)).toStrictEqual([]);
  });

  it("splits tokens at word-diff ranges and fills the changed words with the bar color", () => {
    const content = "const value = next;";
    const tokens = [
      { color: "#ff678d", content: "const ", fontStyle: BOLD },
      { color: null, content: "", fontStyle: null },
      { color: null, content: "value = next;", fontStyle: null },
    ];
    const wordDiffRanges = [rangeOf(content, "next"), rangeOf(content, ";")];

    expect(
      describeRuns(codeRuns({ change: "add", content, id: "l", kind: "line", wordDiffRanges }, tokens, theme)),
    ).toStrictEqual([
      "const  | #ff678d | none | 2",
      `value =  | ${theme.text} | none | 0`,
      `next | ${theme.text} | ${theme.addBar}47 | 0`,
      `; | ${theme.text} | ${theme.addBar}47 | 0`,
    ]);
  });

  it("uses the delete bar for a deletion and ignores ranges on a context line", () => {
    const wordDiffRanges = [rangeOf("ab", "a")];

    expect(
      describeRuns(codeRuns({ change: "delete", content: "ab", id: "l", kind: "line", wordDiffRanges }, [], theme)),
    ).toStrictEqual([`a | ${theme.text} | ${theme.deleteBar}47 | 0`, `b | ${theme.text} | none | 0`]);
    expect(
      describeRuns(codeRuns({ change: "context", content: "ab", id: "l", kind: "line", wordDiffRanges }, [], theme)),
    ).toStrictEqual([`ab | ${theme.text} | none | 0`]);
  });
});

describe("codeRuns on a cut line", () => {
  it("draws a cut line's note in the warning colors from where it starts", () => {
    const content = "code // cut";
    const runs = codeRuns({ content, id: "l", kind: "line", noteStart: content.indexOf("//") }, [], theme);

    expect(describeRuns(runs)).toStrictEqual([
      `code  | ${theme.text} | none | 0`,
      `// cut | ${theme.warningText} | ${theme.warningBackground} | 0`,
    ]);
  });
});

describe("readTokenPatch", () => {
  const tokensByRowId = { line: [{ color: "#fff", content: "x", fontStyle: FIRST_CHUNK }] };

  it("takes a chunk for the current reset key and acknowledges it", () => {
    expect(readTokenPatch(JSON.stringify({ chunkIndex: CHUNK, resetKey: "k", tokensByRowId }), "k")).toStrictEqual({
      delivery: { chunkIndex: CHUNK, message: "tokens-patch-delivered", resetKey: "k" },
      tokensByRowId,
    });
  });

  it("refuses a chunk for another reset key and reports it failed", () => {
    expect(
      readTokenPatch(JSON.stringify({ chunkIndex: FIRST_CHUNK, resetKey: "old", tokensByRowId }), "k"),
    ).toStrictEqual({
      delivery: { chunkIndex: FIRST_CHUNK, message: "tokens-patch-failed", resetKey: "old" },
      tokensByRowId: {},
    });
  });

  it("acknowledges nothing for a patch without a chunk index, and reads a patch without tokens as empty", () => {
    expect(readTokenPatch(JSON.stringify({ tokensByRowId }), "k")).toStrictEqual({ delivery: null, tokensByRowId });
    expect(readTokenPatch(JSON.stringify({ resetKey: "k" }), "k")).toStrictEqual({ delivery: null, tokensByRowId: {} });
  });

  it("fails a chunk whose tokens are not a token map, and reads a patch that is not an object as empty", () => {
    expect(
      readTokenPatch(JSON.stringify({ chunkIndex: CHUNK, resetKey: "k", tokensByRowId: { line: [null] } }), "k"),
    ).toStrictEqual({
      delivery: { chunkIndex: CHUNK, message: "tokens-patch-failed", resetKey: "k" },
      tokensByRowId: {},
    });
    expect(
      readTokenPatch(JSON.stringify({ chunkIndex: CHUNK, resetKey: "k", tokensByRowId: null }), "k"),
    ).toStrictEqual({
      delivery: { chunkIndex: CHUNK, message: "tokens-patch-failed", resetKey: "k" },
      tokensByRowId: {},
    });
    expect(readTokenPatch("null", "k")).toStrictEqual({ delivery: null, tokensByRowId: {} });
    expect(readTokenPatch(JSON.stringify({ chunkIndex: "0", resetKey: 1 }), "k")).toStrictEqual({
      delivery: null,
      tokensByRowId: {},
    });
  });
});
