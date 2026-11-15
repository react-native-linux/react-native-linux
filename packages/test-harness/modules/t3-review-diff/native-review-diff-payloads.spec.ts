import {
  cellsOf,
  contentWidthOf,
  createScrollOffset,
  layoutOf,
  nativeEventOf,
  readRows,
  readStyle,
  readTokenPatch,
  readTokens,
  rowTokensOf,
} from "./src/features/diffs/nativeReviewDiffRows.ts";
import { describe, expect, it } from "vitest";
import type { NativeReviewDiffViewProps } from "./src/features/diffs/nativeReviewDiffSurface.ts";

const ROW_HEIGHT = 20;
const SOURCE_FILE_CONTENT_WIDTH = 32_000;
const DEJAVU_SANS_MONO_ADVANCE = 1233;
const DEJAVU_SANS_MONO_UNITS_PER_EM = 2048;
const MONOSPACE_ADVANCE_PER_EM = DEJAVU_SANS_MONO_ADVANCE / DEJAVU_SANS_MONO_UNITS_PER_EM;
const WIDE_CELLS = 2;
const CJK_RUN = "漢字かなカナ한글";
const CJK_REPEATS = 15;
const COMMENT_PREFIX = "// ";
const NO_OFFSET = 0;
const PANNED_X = 240;
const BOLD = 2;
const CHUNK = 1;
const LINE_NUMBER = 7;
const RANGE_END = 3;
const CODE_FONT_SIZE = 14;
const LINE_ROW = {
  actionable: false,
  additions: 1,
  change: "add",
  commentRangeLabel: "line 7",
  commentText: "x",
  content: "let x;",
  deletions: 0,
  fileId: "a",
  filePath: "a.ts",
  id: "a:line",
  kind: "line",
  newLineNumber: LINE_NUMBER,
  noteStart: RANGE_END,
  oldLineNumber: null,
  previousPath: null,
  text: "x",
  tone: "warning",
  wordDiffRanges: [{ end: RANGE_END, start: 0 }],
};

describe("cellsOf", () => {
  it("counts one cell a code point, two for a wide or fullwidth one, and a surrogate pair as one code point", () => {
    expect(cellsOf("const x = 1;")).toBe("const x = 1;".length);
    expect(cellsOf("中文ＡＢ가")).toBe([..."中文ＡＢ가"].length * WIDE_CELLS);
    expect(cellsOf("𠀀🎉")).toBe([..."𠀀🎉"].length * WIDE_CELLS);
    expect(cellsOf("𝑥é→")).toBe([..."𝑥é→"].length);
  });

  it("gives a long CJK line two cells a character, so its last characters stay reachable", () => {
    const layout = layoutOf({}, ROW_HEIGHT);
    const chrome = layout.changeBarWidth + layout.gutterWidth + layout.codePadding + layout.codePadding;
    const cjkLine = `${COMMENT_PREFIX}${CJK_RUN.repeat(CJK_REPEATS)}`;
    const cells = COMMENT_PREFIX.length + CJK_RUN.length * CJK_REPEATS * WIDE_CELLS;
    const rows = [
      { content: "x".repeat(cjkLine.length), id: "ascii", kind: "line" as const },
      { content: cjkLine, id: "cjk", kind: "line" as const },
    ];

    expect(contentWidthOf(rows, layout, SOURCE_FILE_CONTENT_WIDTH)).toBe(
      chrome + Math.ceil(cells * layout.codeFontSize * MONOSPACE_ADVANCE_PER_EM),
    );
  });
});

describe("createScrollOffset", () => {
  it("starts at no offset and tells its listeners only when x changes", () => {
    const offset = createScrollOffset();
    const seen: number[] = [];
    offset.subscribe(() => seen.push(offset.current()));

    offset.set(NO_OFFSET);
    offset.set(PANNED_X);
    offset.set(PANNED_X);

    expect(seen).toStrictEqual([PANNED_X]);
  });

  it("stops telling a listener once it unsubscribes", () => {
    const offset = createScrollOffset();
    const seen: number[] = [];
    const unsubscribe = offset.subscribe(() => seen.push(offset.current()));

    unsubscribe();
    offset.set(PANNED_X);

    expect(seen).toStrictEqual([]);
    expect(offset.current()).toBe(PANNED_X);
  });
});

describe("nativeEventOf", () => {
  it("carries the payload in a whole synthetic event whose methods a callback can call", () => {
    const event = nativeEventOf("onPressLine", { rowId: "r" });

    expect(event.nativeEvent).toStrictEqual({ rowId: "r" });
    expect(event.dispatchConfig.registrationName).toBe("onPressLine");
    expect([event.isDefaultPrevented(), event.isPropagationStopped()]).toStrictEqual([false, false]);
    event.preventDefault();
    event.stopPropagation();
    event.persist();
    expect([event.isDefaultPrevented(), event.isPropagationStopped()]).toStrictEqual([true, true]);
    expect(event.defaultPrevented).toBe(true);
  });
});

describe("readTokens", () => {
  it("reads a token map, with absent colors and styles, and reads anything else as no tokens", () => {
    const tokens = { line: [{ color: "#fff", content: "x", fontStyle: BOLD }, { content: "y" }] };

    expect(readTokens(JSON.stringify(tokens))).toStrictEqual({
      line: [
        { color: "#fff", content: "x", fontStyle: BOLD },
        { color: null, content: "y", fontStyle: null },
      ],
    });
    expect(readTokens("[]")).toStrictEqual({});
    expect(readTokens(JSON.stringify({ line: "x" }))).toStrictEqual({});
    expect(readTokens(JSON.stringify({ line: [{ color: 1, content: "x" }] }))).toStrictEqual({});
    expect(readTokens(JSON.stringify({ line: [{ content: "x", fontStyle: "bold" }] }))).toStrictEqual({});
    expect(readTokens(JSON.stringify({ line: [{ content: 1 }] }))).toStrictEqual({});
  });
});

describe("readRows", () => {
  it("reads rows whose fields have their declared types, unread fields included", () => {
    const rows = [LINE_ROW, { changeType: "renamed", id: "a", kind: "file", previousPath: "b.ts" }];

    expect(readRows(JSON.stringify(rows))).toStrictEqual(rows);
  });

  it("reads no rows, as T3ReviewDiffView does, when the payload or any one row is malformed", () => {
    const malformed: readonly unknown[] = [
      { ...LINE_ROW, kind: "gutter" },
      { ...LINE_ROW, id: 1 },
      { ...LINE_ROW, content: null },
      { ...LINE_ROW, previousPath: 1 },
      { ...LINE_ROW, change: "move" },
      { ...LINE_ROW, oldLineNumber: "7" },
      { ...LINE_ROW, wordDiffRanges: {} },
      { ...LINE_ROW, wordDiffRanges: [{ start: 0 }] },
      { ...LINE_ROW, tone: "error" },
      { ...LINE_ROW, actionable: "yes" },
      { ...LINE_ROW, changeType: 42 },
      { ...LINE_ROW, changeType: "moved" },
      { ...LINE_ROW, commentSectionTitle: 1 },
      "row",
    ];

    expect(malformed.map((row) => readRows(JSON.stringify([LINE_ROW, row])))).toStrictEqual(malformed.map(() => []));
    expect(readRows(JSON.stringify({ rows: [LINE_ROW] }))).toStrictEqual([]);
  });
});

describe("readStyle", () => {
  it("takes the fields of the declared type, and the default for an absent, mistyped or unknown one", () => {
    const layout = layoutOf({}, ROW_HEIGHT);
    const withoutStyle: Pick<NativeReviewDiffViewProps, "styleJson"> = {};
    const sized = readStyle(JSON.stringify({ codeFontSize: CODE_FONT_SIZE, wordWrap: true }));
    const mistyped = readStyle(
      JSON.stringify({ codeFontSize: "14", codeFontWeight: 600, hunkFontWeight: "constructor" }),
    );

    expect(readStyle(withoutStyle.styleJson)).toStrictEqual({});
    expect(readStyle("[1]")).toStrictEqual({});
    expect(layoutOf(sized, ROW_HEIGHT)).toStrictEqual({ ...layout, codeFontSize: CODE_FONT_SIZE });
    expect(layoutOf(mistyped, ROW_HEIGHT)).toStrictEqual(layout);
  });
});

describe("rowTokensOf", () => {
  it("reads a row's own tokens, and none for a row id the map only inherits", () => {
    const tokens = readTokens(JSON.stringify({ line: [{ content: "x" }] }));

    expect(rowTokensOf(tokens, "line")).toStrictEqual([{ color: null, content: "x", fontStyle: null }]);
    expect(rowTokensOf(tokens, "constructor")).toBeUndefined();
    expect(rowTokensOf(tokens, "missing")).toBeUndefined();
  });
});

describe("readTokenPatch reset keys", () => {
  const tokensByRowId = { line: [{ color: null, content: "x", fontStyle: null }] };

  it("refuses a patch whose reset key is empty for content under another key, and takes it under an empty key", () => {
    const patchJson = JSON.stringify({ chunkIndex: CHUNK, resetKey: "", tokensByRowId });

    expect(readTokenPatch(patchJson, "new")).toStrictEqual({
      delivery: { chunkIndex: CHUNK, message: "tokens-patch-failed", resetKey: "" },
      tokensByRowId: {},
    });
    expect(readTokenPatch(patchJson, "")).toStrictEqual({
      delivery: { chunkIndex: CHUNK, message: "tokens-patch-delivered", resetKey: "" },
      tokensByRowId,
    });
  });
});
