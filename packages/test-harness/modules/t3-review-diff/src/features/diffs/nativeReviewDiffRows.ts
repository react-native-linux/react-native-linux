import type {
  NativeReviewDiffRow,
  NativeReviewDiffStyle,
  NativeReviewDiffTheme,
  NativeReviewDiffToken,
} from "./nativeReviewDiffSurface.ts";

export type NativeReviewDiffTokens = Readonly<Record<string, ReadonlyArray<NativeReviewDiffToken>>>;

export interface NativeReviewDiffLayout {
  readonly rowHeight: number;
  readonly changeBarWidth: number;
  readonly gutterWidth: number;
  readonly codePadding: number;
  readonly fileHeaderHeight: number;
  readonly fileHeaderHorizontalPadding: number;
  readonly codeFontSize: number;
  readonly lineNumberFontSize: number;
  readonly hunkFontSize: number;
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

export interface NativeReviewDiffTokenPatch {
  readonly tokensByRowId: NativeReviewDiffTokens;
  readonly delivery: Readonly<Record<string, unknown>> | null;
}

const COLLAPSED_COMMENT_HEIGHT = 44;
const EXPANDED_COMMENT_HEIGHT = 124;
const MINIMUM_NOTICE_HEIGHT = 44;
const WORD_DIFF_ALPHA = "47";

export function parseIds(json: string | undefined): ReadonlySet<string> {
  return new Set(json ? (JSON.parse(json) as string[]) : []);
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
    lineNumberFontSize: style.lineNumberFontSize ?? 11,
    hunkFontSize: style.hunkFontSize ?? 11,
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
 * Reads a highlighter patch as setTokensPatchJson does: a patch for another reset key adds no tokens, and a
 * chunk is acknowledged through `delivery` so the highlighter sends the next one.
 */
export function readTokenPatch(patchJson: string, resetKey: string | undefined): NativeReviewDiffTokenPatch {
  const patch = JSON.parse(patchJson) as {
    readonly resetKey?: string;
    readonly chunkIndex?: number;
    readonly tokensByRowId?: NativeReviewDiffTokens;
  };
  const accepted = !patch.resetKey || patch.resetKey === resetKey;
  return {
    tokensByRowId: accepted ? (patch.tokensByRowId ?? {}) : {},
    delivery:
      patch.chunkIndex === undefined
        ? null
        : {
            message: accepted ? "tokens-patch-delivered" : "tokens-patch-failed",
            resetKey: patch.resetKey,
            chunkIndex: patch.chunkIndex,
          },
  };
}
