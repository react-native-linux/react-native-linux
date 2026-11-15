import type { NativeSyntheticEvent } from "react-native";

import type {
  NativeReviewDiffRow,
  NativeReviewDiffStyle,
  NativeReviewDiffTheme,
  NativeReviewDiffToken,
} from "./nativeReviewDiffSurface.ts";

export type NativeReviewDiffTokens = Readonly<Record<string, ReadonlyArray<NativeReviewDiffToken>>>;

export type NativeReviewDiffFontWeight = "100" | "200" | "300" | "400" | "500" | "600" | "700" | "800" | "900";

export interface NativeReviewDiffLayout {
  readonly rowHeight: number;
  readonly changeBarWidth: number;
  readonly gutterWidth: number;
  readonly codePadding: number;
  readonly fileHeaderHeight: number;
  readonly fileHeaderHorizontalPadding: number;
  readonly codeFontSize: number;
  readonly codeFontWeight: NativeReviewDiffFontWeight;
  readonly lineNumberFontSize: number;
  readonly lineNumberFontWeight: NativeReviewDiffFontWeight;
  readonly hunkFontSize: number;
  readonly hunkFontWeight: NativeReviewDiffFontWeight;
  readonly fileHeaderFontSize: number;
  readonly fileHeaderMetaFontSize: number;
  readonly fileHeaderSubtextFontSize: number;
}

/** The rows a list shows, by position, with each row's index in the full payload and its frame. */
export interface NativeReviewDiffVisibleRows {
  readonly indexes: ReadonlyArray<number>;
  readonly offsets: ReadonlyArray<number>;
  readonly heights: ReadonlyArray<number>;
}

export interface NativeReviewDiffCodeRun {
  readonly content: string;
  readonly color: string;
  readonly fontStyle: number;
  readonly background: string | undefined;
}

/** The list's horizontal offset, which only the anchored chrome subscribes to. */
export interface NativeReviewDiffScrollOffset {
  readonly current: () => number;
  readonly set: (x: number) => void;
  readonly subscribe: (listener: () => void) => () => void;
}

export interface NativeReviewDiffTokenPatch {
  readonly tokensByRowId: NativeReviewDiffTokens;
  readonly delivery: Readonly<Record<string, unknown>> | null;
}

const COLLAPSED_COMMENT_HEIGHT = 44;
const EXPANDED_COMMENT_HEIGHT = 124;
const MINIMUM_NOTICE_HEIGHT = 44;
const WORD_DIFF_ALPHA = "47";
const EVENT_PHASE_AT_TARGET = 2;
// fontconfig's `monospace` on Ubuntu is DejaVu Sans Mono, 1233 units wide on a 2048-unit em; Menlo and SF Mono are
// within a percent of it.
const MONOSPACE_ADVANCE_PER_EM = 1233 / 2048;
// wcwidth's East Asian Wide and Fullwidth ranges, with the emoji blocks Unicode 9 made wide: two cells each.
const WIDE_CHARACTER =
  /[\u1100-\u115f\u2329\u232a\u2e80-\u303e\u3040-\ua4cf\uac00-\ud7a3\uf900-\ufaff\ufe10-\ufe19\ufe30-\ufe6f\uff00-\uff60\uffe0-\uffe6\u{1f300}-\u{1f64f}\u{1f900}-\u{1f9ff}\u{20000}-\u{2fffd}\u{30000}-\u{3fffd}]/gu;
const SURROGATE_PAIR = /[\ud800-\udbff][\udc00-\udfff]/g;
const FONT_WEIGHTS: Readonly<Record<string, NativeReviewDiffFontWeight>> = {
  thin: "100",
  ultralight: "200",
  "ultra-light": "200",
  light: "300",
  regular: "400",
  medium: "500",
  semibold: "600",
  "semi-bold": "600",
  bold: "700",
  heavy: "800",
  black: "900",
};

/** T3ReviewDiffView's UIFont.Weight names, falling back to bold as it does. */
function fontWeightOf(name: string | undefined): NativeReviewDiffFontWeight {
  return FONT_WEIGHTS[name?.toLowerCase() ?? ""] ?? "700";
}

function isRecord(value: unknown): value is Readonly<Record<string, unknown>> {
  return typeof value === "object" && value !== null && !Array.isArray(value);
}

function isToken(value: unknown): value is NativeReviewDiffToken {
  return (
    isRecord(value) &&
    typeof value["content"] === "string" &&
    (value["color"] == null || typeof value["color"] === "string") &&
    (value["fontStyle"] == null || typeof value["fontStyle"] === "number")
  );
}

function isTokenMap(value: unknown): value is NativeReviewDiffTokens {
  return isRecord(value) && Object.values(value).every((tokens) => Array.isArray(tokens) && tokens.every(isToken));
}

export function parseIds(json: string | undefined): ReadonlySet<string> {
  if (!json) return new Set();
  const ids: unknown = JSON.parse(json);
  if (!Array.isArray(ids) || !ids.every((id) => typeof id === "string")) {
    throw new TypeError(`expected a JSON array of ids, got ${json}`);
  }
  return new Set(ids);
}

/** A tokensJson map, or none when it is not one: a malformed map draws plain text rather than failing the view. */
export function readTokens(json: string): NativeReviewDiffTokens {
  const tokens: unknown = JSON.parse(json);
  return isTokenMap(tokens) ? tokens : {};
}

/** A whole synthetic event for a callback the variant calls itself, as a native view's event arrives. */
export function nativeEventOf<T>(registrationName: string, payload: T): NativeSyntheticEvent<T> {
  let defaultPrevented = false;
  let propagationStopped = false;
  return {
    bubbles: false,
    cancelable: false,
    currentTarget: 0,
    defaultPrevented: false,
    dispatchConfig: { registrationName },
    eventPhase: EVENT_PHASE_AT_TARGET,
    preventDefault: () => {
      defaultPrevented = true;
    },
    isDefaultPrevented: () => defaultPrevented,
    stopPropagation: () => {
      propagationStopped = true;
    },
    isPropagationStopped: () => propagationStopped,
    isTrusted: true,
    nativeEvent: payload,
    persist: () => undefined,
    target: undefined,
    timeStamp: Date.now(),
    type: undefined,
  };
}

export function layoutOf(style: NativeReviewDiffStyle, rowHeight: number): NativeReviewDiffLayout {
  return {
    rowHeight,
    changeBarWidth: style.changeBarWidth ?? 4,
    gutterWidth: style.gutterWidth ?? 46,
    codePadding: style.codePadding ?? 7,
    fileHeaderHeight: style.fileHeaderHeight ?? 56,
    fileHeaderHorizontalPadding: style.fileHeaderHorizontalPadding ?? 10,
    codeFontSize: style.codeFontSize ?? 12,
    codeFontWeight: fontWeightOf(style.codeFontWeight),
    lineNumberFontSize: style.lineNumberFontSize ?? 11,
    lineNumberFontWeight: fontWeightOf(style.lineNumberFontWeight),
    hunkFontSize: style.hunkFontSize ?? 11,
    hunkFontWeight: fontWeightOf(style.hunkFontWeight),
    fileHeaderFontSize: style.fileHeaderFontSize ?? 11,
    fileHeaderMetaFontSize: style.fileHeaderMetaFontSize ?? 10,
    fileHeaderSubtextFontSize: style.fileHeaderSubtextFontSize ?? 11,
  };
}

function rowHeightOf(
  row: NativeReviewDiffRow,
  layout: NativeReviewDiffLayout,
  collapsedCommentIds: ReadonlySet<string>,
): number {
  switch (row.kind) {
    case "file":
      return layout.fileHeaderHeight;
    case "notice":
      return Math.max(layout.rowHeight * 2, MINIMUM_NOTICE_HEIGHT);
    case "comment":
      return collapsedCommentIds.has(row.id) ? COLLAPSED_COMMENT_HEIGHT : EXPANDED_COMMENT_HEIGHT;
    default:
      return layout.rowHeight;
  }
}

/** Hides the rows of collapsed files and lays the rest out from `top`, as the native views do. */
export function visibleRowsOf(
  rows: ReadonlyArray<NativeReviewDiffRow>,
  collapsedFileIds: ReadonlySet<string>,
  collapsedCommentIds: ReadonlySet<string>,
  layout: NativeReviewDiffLayout,
  top: number,
): NativeReviewDiffVisibleRows {
  const indexes: number[] = [];
  const offsets: number[] = [];
  const heights: number[] = [];
  let offset = top;
  let fileCollapsed = false;
  rows.forEach((row, index) => {
    if (row.kind === "file") fileCollapsed = collapsedFileIds.has(row.fileId ?? "");
    else if (fileCollapsed) return;
    const height = rowHeightOf(row, layout, collapsedCommentIds);
    indexes.push(index);
    offsets.push(offset);
    heights.push(height);
    offset += height;
  });
  return { indexes, offsets, heights };
}

/** Monospace cells, as wcwidth counts them: one per code point, two for an East Asian Wide or Fullwidth one. */
export function cellsOf(text: string): number {
  return text.length - (text.match(SURROGATE_PAIR)?.length ?? 0) + (text.match(WIDE_CHARACTER)?.length ?? 0);
}

/**
 * The width the rows scroll over: the chrome, the longest line or hunk header in monospace, and the code padding on
 * both sides, as T3ReviewDiffView's contentWidthsByFileId measures it, but never wider than `contentWidth`.
 */
export function contentWidthOf(
  rows: ReadonlyArray<NativeReviewDiffRow>,
  layout: NativeReviewDiffLayout,
  contentWidth: number,
): number {
  let codeWidth = 0;
  for (const row of rows) {
    if (row.kind === "line") codeWidth = Math.max(codeWidth, cellsOf(row.content ?? "") * layout.codeFontSize);
    else if (row.kind === "hunk") codeWidth = Math.max(codeWidth, cellsOf(row.text || row.content || "") * layout.hunkFontSize);
  }
  const chrome = layout.changeBarWidth + layout.gutterWidth + layout.codePadding * 2;
  return Math.min(contentWidth, chrome + Math.ceil(codeWidth * MONOSPACE_ADVANCE_PER_EM));
}

/** A store for `useSyncExternalStore` that tells its listeners only when x changes, not on a vertical scroll. */
export function createScrollOffset(): NativeReviewDiffScrollOffset {
  let x = 0;
  const listeners = new Set<() => void>();
  return {
    current: () => x,
    set: (next) => {
      if (next === x) return;
      x = next;
      for (const listener of listeners) listener();
    },
    subscribe: (listener) => {
      listeners.add(listener);
      return () => {
        listeners.delete(listener);
      };
    },
  };
}

/** Splits a line's tokens where a word-diff range or a cut line's note starts or ends. */
export function codeRuns(
  row: NativeReviewDiffRow,
  tokens: ReadonlyArray<NativeReviewDiffToken> | undefined,
  theme: NativeReviewDiffTheme,
): NativeReviewDiffCodeRun[] {
  const source =
    tokens && tokens.length > 0
      ? tokens
      : [{ content: row.content ?? "", color: null, fontStyle: null }];
  const ranges = row.change === "add" || row.change === "delete" ? (row.wordDiffRanges ?? []) : [];
  const highlight = `${row.change === "add" ? theme.addBar : theme.deleteBar}${WORD_DIFF_ALPHA}`;
  const noteStart = row.noteStart ?? Number.POSITIVE_INFINITY;
  const cuts = [...ranges.flatMap((range) => [range.start, range.end]), noteStart];
  const runs: NativeReviewDiffCodeRun[] = [];
  let offset = 0;
  for (const token of source) {
    const end = offset + token.content.length;
    const inner = cuts.filter((cut) => cut > offset && cut < end).sort((left, right) => left - right);
    let start = offset;
    for (const stop of [...inner, end]) {
      if (stop > start) {
        const note = start >= noteStart;
        runs.push({
          content: token.content.slice(start - offset, stop - offset),
          color: note ? theme.warningText : (token.color ?? theme.text),
          fontStyle: token.fontStyle ?? 0,
          background: note
            ? theme.warningBackground
            : ranges.some((range) => start >= range.start && start < range.end)
              ? highlight
              : undefined,
        });
      }
      start = stop;
    }
    offset = end;
  }
  return runs;
}

/**
 * Reads a highlighter patch as setTokensPatchJson does: a patch for another reset key, or one that is not a token
 * map, adds no tokens, and a chunk is acknowledged through `delivery` so the highlighter sends the next one.
 */
export function readTokenPatch(patchJson: string, resetKey: string | undefined): NativeReviewDiffTokenPatch {
  const parsed: unknown = JSON.parse(patchJson);
  const patch = isRecord(parsed) ? parsed : {};
  const patchResetKey = typeof patch["resetKey"] === "string" ? patch["resetKey"] : undefined;
  const received = patch["tokensByRowId"] ?? {};
  const tokens = isTokenMap(received) && (!patchResetKey || patchResetKey === resetKey) ? received : null;
  return {
    tokensByRowId: tokens ?? {},
    delivery:
      typeof patch["chunkIndex"] === "number"
        ? {
            message: tokens ? "tokens-patch-delivered" : "tokens-patch-failed",
            resetKey: patchResetKey,
            chunkIndex: patch["chunkIndex"],
          }
        : null,
  };
}
