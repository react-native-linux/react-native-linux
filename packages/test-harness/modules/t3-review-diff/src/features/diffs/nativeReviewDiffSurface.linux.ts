import {
  createElement,
  memo,
  useCallback,
  useEffect,
  useImperativeHandle,
  useLayoutEffect,
  useMemo,
  useRef,
  useState,
  useSyncExternalStore,
  type ComponentType,
  type ReactElement,
  type ReactNode,
  type RefObject,
} from "react";
import {
  FlatList,
  Pressable,
  Text,
  View,
  type FlatListProps,
  type LayoutChangeEvent,
  type ListRenderItemInfo,
  type NativeScrollEvent,
  type NativeSyntheticEvent,
  type ViewStyle,
} from "react-native";

import {
  codeRuns,
  contentWidthOf,
  createScrollOffset,
  layoutOf,
  nativeEventOf,
  parseIds,
  readTokenPatch,
  readTokens,
  visibleRowsOf,
  type NativeReviewDiffLayout as Layout,
  type NativeReviewDiffScrollOffset as ScrollOffset,
  type NativeReviewDiffTokens as Tokens,
} from "./nativeReviewDiffRows";
import type {
  NativeReviewDiffRow,
  NativeReviewDiffStyle,
  NativeReviewDiffTheme,
  NativeReviewDiffToken,
  NativeReviewDiffViewProps,
} from "./nativeReviewDiffSurface";

// Linux has no T3ReviewDiffView. Metro resolves this file before nativeReviewDiffSurface.ts, so the review
// sheet gets the same props and events, drawn by React Native rows in a FlatList. styleJson's wordWrap is not
// read: lines never wrap (react-native-linux/react-native-linux#712).

interface RowHandlers {
  readonly press: (row: NativeReviewDiffRow, gesture: "tap" | "longPress") => void;
  readonly toggleViewed: (row: NativeReviewDiffRow) => void;
}

const SELECTED_BAR_ALPHA = "f2";
const MONOSPACE = "monospace";
const REFRESH_BAR_HEIGHT = 2;

interface DiffRowProps {
  readonly row: NativeReviewDiffRow;
  readonly height: number;
  readonly tokens: ReadonlyArray<NativeReviewDiffToken> | undefined;
  readonly collapsed: boolean;
  readonly viewed: boolean;
  readonly selected: boolean;
  readonly viewportWidth: number;
  readonly offset: ScrollOffset;
  readonly layout: Layout;
  readonly theme: NativeReviewDiffTheme;
  readonly handlers: RowHandlers;
}

function FileHeaderRow({ row, height, collapsed, viewed, layout, theme, handlers }: DiffRowProps) {
  const path =
    row.previousPath && row.previousPath !== row.filePath
      ? `${row.previousPath} -> ${row.filePath ?? ""}`
      : (row.filePath ?? "");
  const metaText = { fontSize: layout.fileHeaderMetaFontSize, fontWeight: "600" } as const;
  return createElement(
    View,
    {
      style: {
        height,
        flexDirection: "row",
        alignItems: "center",
        paddingHorizontal: layout.fileHeaderHorizontalPadding,
        backgroundColor: theme.headerBackground,
        borderBottomWidth: 1,
        borderColor: theme.border,
      },
    },
    createElement(
      Pressable,
      {
        accessibilityRole: "button",
        accessibilityState: { expanded: !collapsed },
        onPress: () => handlers.press(row, "tap"),
        style: { flex: 1, flexDirection: "row", alignItems: "center", alignSelf: "stretch" },
      },
      createElement(
        Text,
        {
          style: {
            width: 20,
            color: theme.mutedText,
            fontSize: 16,
            textAlign: "center",
            transform: [{ rotate: collapsed ? "0deg" : "90deg" }],
          },
        },
        "›",
      ),
      createElement(
        Text,
        { numberOfLines: 1, style: { flex: 1, marginLeft: 8, color: theme.text, fontSize: layout.fileHeaderFontSize, fontWeight: "600" } },
        path,
      ),
    ),
    createElement(Text, { style: [metaText, { color: theme.deleteText, marginLeft: 12 }] }, `-${row.deletions ?? 0}`),
    createElement(Text, { style: [metaText, { color: theme.addText, marginLeft: 4 }] }, `+${row.additions ?? 0}`),
    createElement(Pressable, {
      accessibilityRole: "checkbox",
      accessibilityState: { checked: viewed },
      onPress: () => handlers.toggleViewed(row),
      style: {
        width: 20,
        height: 20,
        marginLeft: 10,
        borderRadius: 5,
        borderWidth: 1.5,
        borderColor: viewed ? theme.addBar : theme.mutedText,
        backgroundColor: viewed ? theme.addBar : "transparent",
      },
    }),
  );
}

function LineRow({ row, height, tokens, selected, offset, layout, theme, handlers }: DiffRowProps) {
  const change = row.change ?? "context";
  const lineNumber = row.newLineNumber ?? row.oldLineNumber;
  const background =
    change === "add" ? theme.addBackground : change === "delete" ? theme.deleteBackground : theme.background;
  const bar = selected
    ? `${theme.hunkText}${SELECTED_BAR_ALPHA}`
    : change === "add"
      ? theme.addBar
      : change === "delete"
        ? theme.deleteBar
        : "transparent";
  return createElement(
    Pressable,
    {
      onPress: () => handlers.press(row, "tap"),
      onLongPress: () => handlers.press(row, "longPress"),
      style: { height, backgroundColor: background },
    },
    createElement(
      Text,
      {
        numberOfLines: 1,
        style: {
          paddingLeft: layout.changeBarWidth + layout.gutterWidth + layout.codePadding,
          lineHeight: height,
          fontFamily: MONOSPACE,
          fontSize: layout.codeFontSize,
          fontWeight: layout.codeFontWeight,
          color: theme.text,
        },
      },
      codeRuns(row, tokens, theme).map((run, index) =>
        createElement(
          Text,
          {
            key: index,
            style: {
              color: run.color,
              backgroundColor: run.background,
              fontWeight: run.fontStyle & 2 ? "700" : layout.codeFontWeight,
              fontStyle: run.fontStyle & 1 ? "italic" : "normal",
              textDecorationLine: run.fontStyle & 4 ? "underline" : "none",
            },
          },
          run.content,
        ),
      ),
    ),
    createElement(
      Anchored,
      {
        offset,
        style: {
          position: "absolute",
          top: 0,
          bottom: 0,
          left: 0,
          width: layout.changeBarWidth + layout.gutterWidth,
          flexDirection: "row",
          backgroundColor: background,
        },
      },
      createElement(View, { style: { width: layout.changeBarWidth, backgroundColor: bar } }),
      createElement(
        Text,
        {
          style: {
            flex: 1,
            paddingRight: layout.codePadding,
            textAlign: "right",
            lineHeight: height,
            fontFamily: MONOSPACE,
            fontSize: layout.lineNumberFontSize,
            fontWeight: layout.lineNumberFontWeight,
            color: change === "add" ? theme.addText : change === "delete" ? theme.deleteText : theme.mutedText,
          },
        },
        lineNumber == null ? "" : String(lineNumber),
      ),
    ),
  );
}

function HunkRow({ row, height, layout, theme }: DiffRowProps) {
  return createElement(
    View,
    { style: { height, justifyContent: "center", backgroundColor: theme.hunkBackground } },
    createElement(
      Text,
      {
        numberOfLines: 1,
        style: {
          paddingLeft: layout.changeBarWidth + layout.gutterWidth + layout.codePadding,
          fontFamily: MONOSPACE,
          fontSize: layout.hunkFontSize,
          fontWeight: layout.hunkFontWeight,
          color: theme.hunkText,
        },
      },
      row.text || row.content || "",
    ),
  );
}

function NoticeRow({ row, height, layout, theme, handlers }: DiffRowProps) {
  const warning = row.tone === "warning";
  return createElement(
    Pressable,
    {
      disabled: !row.actionable,
      onPress: () => handlers.press(row, "tap"),
      style: {
        height,
        justifyContent: "center",
        paddingHorizontal: layout.fileHeaderHorizontalPadding,
        backgroundColor: warning ? theme.warningBackground : theme.background,
        borderBottomWidth: 1,
        borderColor: theme.border,
      },
    },
    createElement(
      Text,
      {
        numberOfLines: 1,
        style: { fontSize: layout.fileHeaderSubtextFontSize, fontWeight: "500", color: warning ? theme.warningText : theme.mutedText },
      },
      row.text ?? "",
    ),
  );
}

function CommentRow({ row, height, collapsed, layout, theme, handlers }: DiffRowProps) {
  const subtext = { fontSize: layout.fileHeaderSubtextFontSize, fontWeight: "500" } as const;
  return createElement(
    Pressable,
    {
      accessibilityRole: "button",
      accessibilityState: { expanded: !collapsed },
      onPress: () => handlers.press(row, "tap"),
      style: { height, paddingHorizontal: 8, paddingVertical: 5, backgroundColor: theme.background },
    },
    createElement(
      View,
      {
        style: {
          flex: 1,
          paddingHorizontal: 18,
          paddingVertical: 12,
          borderRadius: 10,
          borderWidth: 1,
          borderColor: theme.border,
          backgroundColor: theme.headerBackground,
        },
      },
      createElement(
        Text,
        { numberOfLines: 1, style: [subtext, { color: theme.mutedText }] },
        `Comment on ${row.commentRangeLabel || "line"}`,
      ),
      collapsed
        ? null
        : createElement(
            Text,
            { numberOfLines: 3, style: [subtext, { marginTop: 10, color: theme.text }] },
            row.commentText || "Comment",
          ),
    ),
  );
}

const ANCHORED_ROWS = { file: FileHeaderRow, notice: NoticeRow, comment: CommentRow } as const;

// Headers, notices and comments span the viewport and the gutter stays at its left edge while the code scrolls
// sideways under them, as T3ReviewDiffView draws them. Only this view re-renders on a sideways scroll: its children
// are the elements its row made, so the code and its token runs keep their last render. Native-driven Animated
// transforms on every mounted row instead read p50 37.8 ms over t3-review-diff-120hz's scroll steps.
function Anchored({ offset, style, children }: { readonly offset: ScrollOffset; readonly style: ViewStyle; readonly children: ReactNode }) {
  const x = useSyncExternalStore(offset.subscribe, offset.current);
  return createElement(View, { style: [style, { transform: [{ translateX: x }] }] }, children);
}

const DiffRow = memo(function DiffRow(props: DiffRowProps) {
  const { kind } = props.row;
  if (kind === "line") return createElement(LineRow, props);
  if (kind === "hunk") return createElement(HunkRow, props);
  return createElement(
    Anchored,
    { offset: props.offset, style: { width: props.viewportWidth } },
    createElement(ANCHORED_ROWS[kind], props),
  );
});

interface ReviewDiffListProps extends Omit<FlatListProps<number>, "renderItem" | "onScroll"> {
  readonly listRef: RefObject<FlatList<number> | null>;
  readonly renderRow: (item: number, index: number, offset: ScrollOffset) => ReactElement | null;
}

// Keyed by contentResetKey: new content starts the list and its anchored chrome at x 0 together.
function ReviewDiffList({ listRef, renderRow, ...listProps }: ReviewDiffListProps) {
  const [offset] = useState(createScrollOffset);
  const renderItem = useCallback(
    ({ item, index }: ListRenderItemInfo<number>) => renderRow(item, index, offset),
    [renderRow, offset],
  );
  return createElement(FlatList<number>, {
    ...listProps,
    ref: listRef,
    renderItem,
    onScroll: (event: NativeSyntheticEvent<NativeScrollEvent>) => offset.set(event.nativeEvent.contentOffset.x),
  });
}

function useTokens(
  props: NativeReviewDiffViewProps,
  report: (payload: Record<string, unknown>) => void,
): Tokens {
  const { tokensJson, tokensPatchJson, tokensResetKey } = props;
  const [tokens, setTokens] = useState<Tokens>({});
  const resetKeyRef = useRef(tokensResetKey);
  // oxlint-disable-next-line t3code/no-unreasoned-state-sync -- a new reset key drops every token of the old content
  useEffect(() => {
    resetKeyRef.current = tokensResetKey;
    setTokens({});
  }, [tokensResetKey]);
  // oxlint-disable-next-line t3code/no-unreasoned-state-sync -- tokensJson replaces the whole token map, as setTokensJson does
  useEffect(() => {
    if (tokensJson !== undefined) setTokens(readTokens(tokensJson));
  }, [tokensJson]);
  // oxlint-disable-next-line t3code/no-unreasoned-state-sync -- merges a highlighter patch and acknowledges it, as setTokensPatchJson does
  useEffect(() => {
    if (tokensPatchJson === undefined) return;
    const { tokensByRowId, delivery } = readTokenPatch(tokensPatchJson, resetKeyRef.current);
    if (Object.keys(tokensByRowId).length > 0) setTokens((current) => ({ ...current, ...tokensByRowId }));
    if (delivery) report(delivery);
  }, [tokensPatchJson, report]);
  return tokens;
}

function LinuxReviewDiffView(props: NativeReviewDiffViewProps) {
  const {
    rowsJson,
    themeJson,
    styleJson,
    rowHeight,
    contentWidth,
    contentInsetTop = 0,
    contentInsetBottom = 0,
    collapsedFileIdsJson,
    viewedFileIdsJson,
    selectedRowIdsJson,
    collapsedCommentIdsJson,
    initialFileId,
    initialRowIndex,
    contentResetKey,
    nativeViewRef,
    scrollEnabled,
    bounces,
    refreshing,
    style,
    tokensJson,
    tokensPatchJson,
    tokensResetKey,
    appearanceScheme,
    onPullToRefresh,
    onDebug,
    onVisibleFileChange,
    onToggleFile,
    onToggleViewedFile,
    onPressLine,
    onToggleComment,
    onPressNotice,
    ...viewProps
  } = props;
  const rows = useMemo(() => JSON.parse(rowsJson) as NativeReviewDiffRow[], [rowsJson]);
  const theme = useMemo(() => JSON.parse(themeJson) as NativeReviewDiffTheme, [themeJson]);
  const layout = useMemo(
    () => layoutOf(styleJson ? (JSON.parse(styleJson) as NativeReviewDiffStyle) : {}, rowHeight),
    [styleJson, rowHeight],
  );
  const collapsedFileIds = useMemo(() => parseIds(collapsedFileIdsJson), [collapsedFileIdsJson]);
  const viewedFileIds = useMemo(() => parseIds(viewedFileIdsJson), [viewedFileIdsJson]);
  const selectedRowIds = useMemo(() => parseIds(selectedRowIdsJson), [selectedRowIdsJson]);
  const collapsedCommentIds = useMemo(() => parseIds(collapsedCommentIdsJson), [collapsedCommentIdsJson]);
  const [viewportWidth, setViewportWidth] = useState(0);
  const width = useMemo(() => contentWidthOf(rows, layout, contentWidth), [rows, layout, contentWidth]);

  // The highlighter reads row indexes of the full payload, including the rows of collapsed files.
  const visible = useMemo(
    () => visibleRowsOf(rows, collapsedFileIds, collapsedCommentIds, layout, contentInsetTop),
    [rows, collapsedFileIds, collapsedCommentIds, layout, contentInsetTop],
  );

  const latest = useRef({ props, rows, visible });
  useLayoutEffect(() => {
    latest.current = { props, rows, visible };
  });
  const report = useCallback(
    (payload: Record<string, unknown>) => latest.current.props.onDebug?.(nativeEventOf("onDebug", payload)),
    [],
  );
  const tokens = useTokens(props, report);

  const handlers = useMemo<RowHandlers>(
    () => ({
      press: (row, gesture) => {
        const current = latest.current.props;
        const fileId = row.fileId ?? "";
        if (row.kind === "file") {
          if (gesture === "tap") current.onToggleFile?.(nativeEventOf("onToggleFile", { fileId }));
        } else if (row.kind === "comment") {
          current.onToggleComment?.(nativeEventOf("onToggleComment", { commentId: row.id }));
        } else if (row.kind === "notice") {
          if (gesture === "tap" && row.actionable) current.onPressNotice?.(nativeEventOf("onPressNotice", { rowId: row.id, fileId }));
        } else if (row.kind === "line") {
          current.onPressLine?.(
            nativeEventOf("onPressLine", {
              rowId: row.id,
              fileId,
              gesture,
              change: row.change ?? "context",
              ...(row.oldLineNumber == null ? {} : { oldLineNumber: row.oldLineNumber }),
              ...(row.newLineNumber == null ? {} : { newLineNumber: row.newLineNumber }),
            }),
          );
        }
      },
      toggleViewed: (row) => latest.current.props.onToggleViewedFile?.(nativeEventOf("onToggleViewedFile", { fileId: row.fileId ?? "" })),
    }),
    [],
  );

  const lastVisibleFileId = useRef<string | null>(null);
  const onViewableItemsChanged = useCallback(({ viewableItems }: { viewableItems: ReadonlyArray<{ index?: number | null }> }) => {
    const positions = viewableItems.flatMap((item) => (item.index == null ? [] : [item.index]));
    if (positions.length === 0) return;
    const { props: current, rows: allRows, visible: shown } = latest.current;
    const firstRowIndex = shown.indexes[Math.min(...positions)] ?? 0;
    report({
      message: "visible-range",
      firstRowIndex,
      lastRowIndex: shown.indexes[Math.max(...positions)] ?? firstRowIndex,
      totalRows: allRows.length,
    });
    const fileId = allRows[firstRowIndex]?.fileId;
    if (fileId && fileId !== lastVisibleFileId.current) {
      lastVisibleFileId.current = fileId;
      current.onVisibleFileChange?.(nativeEventOf("onVisibleFileChange", { fileId }));
    }
  }, [report]);

  const listRef = useRef<FlatList<number>>(null);
  useImperativeHandle(
    nativeViewRef,
    () => ({
      scrollToFile: async (fileId, animated = true) => {
        const { rows: allRows, visible: shown } = latest.current;
        const index = shown.indexes.findIndex((rowIndex) => {
          const row = allRows[rowIndex];
          return row?.kind === "file" && row.fileId === fileId;
        });
        if (index >= 0) listRef.current?.scrollToIndex({ index, animated });
      },
      scrollToTop: async (animated = true) => {
        listRef.current?.scrollToOffset({ offset: 0, animated });
      },
    }),
    [],
  );

  // oxlint-disable-next-line t3code/no-unreasoned-state-sync -- reports the mount the way the native surface does, once
  useEffect(() => {
    report({ message: "surface-content-mounted" });
  }, [report]);

  const initialScrollIndex = useMemo(() => {
    const index = visible.indexes.findIndex((rowIndex) =>
      initialFileId
        ? rows[rowIndex]?.kind === "file" && rows[rowIndex]?.fileId === initialFileId
        : rowIndex >= (initialRowIndex ?? 0),
    );
    return index > 0 ? index : undefined;
  }, [visible, rows, initialFileId, initialRowIndex]);

  const rowViewportWidth = viewportWidth || width;
  const renderRow = useCallback(
    (item: number, index: number, offset: ScrollOffset) => {
      const row = rows[item];
      if (!row) return null;
      const fileId = row.fileId ?? "";
      return createElement(DiffRow, {
        row,
        height: visible.heights[index] ?? layout.rowHeight,
        tokens: tokens[row.id],
        collapsed: row.kind === "comment" ? collapsedCommentIds.has(row.id) : collapsedFileIds.has(fileId),
        viewed: viewedFileIds.has(fileId),
        selected: selectedRowIds.has(row.id),
        viewportWidth: rowViewportWidth,
        offset,
        layout,
        theme,
        handlers,
      });
    },
    [rows, visible, tokens, collapsedCommentIds, collapsedFileIds, viewedFileIds, selectedRowIds, rowViewportWidth, layout, theme, handlers],
  );

  return createElement(
    View,
    {
      ...viewProps,
      onLayout: (event: LayoutChangeEvent) => {
        setViewportWidth(event.nativeEvent.layout.width);
        viewProps.onLayout?.(event);
      },
      style: [style, { backgroundColor: theme.background }],
    },
    createElement(ReviewDiffList, {
      key: contentResetKey,
      listRef,
      data: visible.indexes,
      keyExtractor: (item: number) => rows[item]?.id ?? String(item),
      renderRow,
      getItemLayout: (_data: unknown, index: number) => ({
        index,
        length: visible.heights[index] ?? 0,
        offset: visible.offsets[index] ?? 0,
      }),
      initialScrollIndex,
      onViewableItemsChanged,
      viewabilityConfig: { itemVisiblePercentThreshold: 1 },
      scrollEnabled,
      bounces,
      showsHorizontalScrollIndicator: false,
      windowSize: 3,
      maxToRenderPerBatch: 40,
      initialNumToRender: 40,
      contentContainerStyle: {
        width: Math.max(width, viewportWidth),
        paddingTop: contentInsetTop,
        paddingBottom: contentInsetBottom,
      },
      style: { flex: 1 },
    }),
    refreshing
      ? createElement(View, {
          accessibilityRole: "progressbar",
          style: {
            position: "absolute",
            top: contentInsetTop,
            left: 0,
            right: 0,
            height: REFRESH_BAR_HEIGHT,
            backgroundColor: theme.hunkText,
          },
        })
      : null,
  );
}

export function isNativeReviewDiffDrawEvent(payload: Readonly<Record<string, unknown>>): boolean {
  return payload.message === "draw-metrics" || payload.message === "visible-range";
}

export function resolveNativeReviewDiffView(): ComponentType<NativeReviewDiffViewProps> {
  return LinuxReviewDiffView;
}
