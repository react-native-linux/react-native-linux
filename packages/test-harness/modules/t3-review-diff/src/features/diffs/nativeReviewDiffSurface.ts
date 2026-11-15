import type { Ref } from "react";
import type { NativeSyntheticEvent, ViewProps } from "react-native";

// The types of T3 Code's own nativeReviewDiffSurface.ts, so the Linux variant beside it compiles here.

export interface NativeReviewDiffRow {
  readonly kind: "file" | "hunk" | "line" | "notice" | "comment";
  readonly id: string;
  readonly fileId?: string;
  readonly filePath?: string;
  readonly previousPath?: string | null;
  readonly changeType?:
    | "modified"
    | "new"
    | "deleted"
    | "renamed"
    | "rename-pure"
    | "rename-changed";
  readonly additions?: number;
  readonly deletions?: number;
  readonly text?: string;
  readonly content?: string;
  readonly change?: "context" | "add" | "delete";
  readonly oldLineNumber?: number | null;
  readonly newLineNumber?: number | null;
  readonly wordDiffRanges?: ReadonlyArray<NativeReviewDiffWordDiffRange>;
  readonly commentText?: string;
  readonly commentRangeLabel?: string;
  readonly commentSectionTitle?: string;
  /** A notice drawn in the warning colors. */
  readonly tone?: "warning";
  /** A notice that reports taps through `onPressNotice`. */
  readonly actionable?: boolean;
  /** Where a cut line's note starts in `content`. */
  readonly noteStart?: number;
}

export interface NativeReviewDiffWordDiffRange {
  readonly start: number;
  readonly end: number;
}

export interface NativeReviewDiffToken {
  readonly content: string;
  readonly color: string | null;
  readonly fontStyle: number | null;
}

export interface NativeReviewDiffTheme {
  readonly background: string;
  readonly text: string;
  readonly mutedText: string;
  readonly headerBackground: string;
  readonly border: string;
  readonly hunkBackground: string;
  readonly hunkText: string;
  readonly addBackground: string;
  readonly deleteBackground: string;
  readonly addBar: string;
  readonly deleteBar: string;
  readonly addText: string;
  readonly deleteText: string;
  readonly warningText: string;
  readonly warningBackground: string;
}

export interface NativeReviewDiffStyle {
  readonly rowHeight?: number;
  readonly contentWidth?: number;
  readonly changeBarWidth?: number;
  readonly gutterWidth?: number;
  readonly codePadding?: number;
  readonly textVerticalInset?: number;
  readonly fileHeaderHeight?: number;
  readonly fileHeaderHorizontalMargin?: number;
  readonly fileHeaderVerticalMargin?: number;
  readonly fileHeaderCornerRadius?: number;
  readonly fileHeaderHorizontalPadding?: number;
  readonly fileHeaderPathRightPadding?: number;
  readonly fileHeaderCountColumnWidth?: number;
  readonly fileHeaderCountGap?: number;
  readonly codeFontSize?: number;
  readonly codeFontWeight?: string;
  readonly lineNumberFontSize?: number;
  readonly lineNumberFontWeight?: string;
  readonly hunkFontSize?: number;
  readonly hunkFontWeight?: string;
  readonly fileHeaderFontSize?: number;
  readonly fileHeaderFontWeight?: string;
  readonly fileHeaderMetaFontSize?: number;
  readonly fileHeaderMetaFontWeight?: string;
  readonly fileHeaderSubtextFontSize?: number;
  readonly fileHeaderSubtextFontWeight?: string;
  readonly fileHeaderStatusFontSize?: number;
  readonly fileHeaderStatusFontWeight?: string;
  readonly emptyStateFontSize?: number;
  readonly emptyStateFontWeight?: string;
}

export interface NativeReviewDiffViewProps extends ViewProps {
  readonly contentInsetTop?: number;
  readonly contentInsetBottom?: number;
  readonly rowsJson: string;
  readonly tokensJson?: string;
  readonly tokensPatchJson?: string;
  readonly tokensResetKey?: string;
  readonly contentResetKey?: string;
  readonly collapsedFileIdsJson?: string;
  readonly viewedFileIdsJson?: string;
  readonly selectedRowIdsJson?: string;
  readonly collapsedCommentIdsJson?: string;
  readonly appearanceScheme: "light" | "dark";
  readonly themeJson: string;
  readonly styleJson?: string;
  readonly rowHeight: number;
  /** Android ignores this and measures the widest row. */
  readonly contentWidth: number;
  readonly initialRowIndex?: number;
  readonly initialFileId?: string;
  readonly scrollEnabled?: boolean;
  readonly bounces?: boolean;
  readonly refreshing?: boolean;
  readonly nativeViewRef?: Ref<NativeReviewDiffViewHandle>;
  readonly onPullToRefresh?: (event: NativeSyntheticEvent<Record<string, never>>) => void;
  readonly onDebug?: (event: NativeSyntheticEvent<Record<string, unknown>>) => void;
  readonly onVisibleFileChange?: (
    event: NativeSyntheticEvent<{ readonly fileId?: string | null }>,
  ) => void;
  readonly onToggleFile?: (event: NativeSyntheticEvent<{ readonly fileId?: string }>) => void;
  readonly onToggleViewedFile?: (event: NativeSyntheticEvent<{ readonly fileId?: string }>) => void;
  readonly onPressLine?: (
    event: NativeSyntheticEvent<{
      readonly rowId?: string;
      readonly fileId?: string;
      readonly gesture?: "tap" | "longPress";
      readonly oldLineNumber?: number;
      readonly newLineNumber?: number;
      readonly change?: "context" | "add" | "delete";
    }>,
  ) => void;
  readonly onToggleComment?: (event: NativeSyntheticEvent<{ readonly commentId?: string }>) => void;
  readonly onPressNotice?: (
    event: NativeSyntheticEvent<{ readonly rowId?: string; readonly fileId?: string }>,
  ) => void;
}

export interface NativeReviewDiffViewHandle {
  readonly scrollToFile: (fileId: string, animated?: boolean) => Promise<void>;
  readonly scrollToTop: (animated?: boolean) => Promise<void>;
}
