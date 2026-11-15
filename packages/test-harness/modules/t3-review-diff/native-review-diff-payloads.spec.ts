import {
  cellsOf,
  contentWidthOf,
  createScrollOffset,
  layoutOf,
  nativeEventOf,
  readTokens,
} from "./src/features/diffs/nativeReviewDiffRows.ts";
import { describe, expect, it } from "vitest";

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
  });
});

describe("readTokens", () => {
  it("reads a token map, with absent colors and styles, and reads anything else as no tokens", () => {
    const tokens = { line: [{ color: "#fff", content: "x", fontStyle: BOLD }, { content: "y" }] };

    expect(readTokens(JSON.stringify(tokens))).toStrictEqual(tokens);
    expect(readTokens("[]")).toStrictEqual({});
    expect(readTokens(JSON.stringify({ line: "x" }))).toStrictEqual({});
    expect(readTokens(JSON.stringify({ line: [{ color: 1, content: "x" }] }))).toStrictEqual({});
    expect(readTokens(JSON.stringify({ line: [{ content: "x", fontStyle: "bold" }] }))).toStrictEqual({});
    expect(readTokens(JSON.stringify({ line: [{ content: 1 }] }))).toStrictEqual({});
  });
});
