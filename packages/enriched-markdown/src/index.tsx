import { Text, View } from "react-native";
import { boxOf, headingElement, noMarkdownStyle, resolveStyle, restyle, spanStyles, textOf } from "./styles.ts";
import { lexAppended, linksOf, nothingLexed } from "./lex.ts";
import { memo, useLayoutEffect, useMemo, useRef, useState } from "react";

type MarkdownStyle = typeof noMarkdownStyle;
type ResolvedStyle = ReturnType<typeof resolveStyle>;
type LexedMarkdown = ReturnType<typeof lexAppended>;
type MarkdownNode = LexedMarkdown["blocks"][number]["node"];
type LinkPressHandler = (event: { readonly url: string }) => void;
type LinksChangeHandler = (links: ReturnType<typeof linksOf>) => void;

interface EnrichedMarkdownTextProps {
  readonly containerStyle?: React.ComponentProps<typeof View>["style"];
  readonly markdown: string;
  readonly markdownStyle?: MarkdownStyle;
  readonly md4cFlags?: { readonly hardSoftBreaks?: boolean };
  readonly onLinkPress?: LinkPressHandler;
  readonly onLinksChange?: LinksChangeHandler;
  readonly selectable?: boolean;
}

interface RenderContext {
  readonly breaks: Readonly<Record<string, string>>;
  readonly onLinkPress: LinkPressHandler;
  readonly selectable: boolean;
  readonly spans: ReturnType<typeof spanStyles>;
  readonly style: ResolvedStyle;
}

type RenderChildren = (nodes: readonly MarkdownNode[], context: RenderContext) => React.ReactNode;

const NO_CHILDREN = 0;
const FIRST_ORDINAL = 1;
const HEADING_LEVEL_OFFSET = "h".length;

const renderInline = (nodes: readonly MarkdownNode[], context: RenderContext): React.ReactNode =>
  nodes.map(({ children, token }, index) => {
    const key = String(index);
    const content = children.length === NO_CHILDREN ? token.content : renderInline(children, context);
    const spanStyle = context.spans[token.type];
    const href = token.attrGet("href");

    if (href !== null) {
      return (
        <Text key={key} onPress={(): void => context.onLinkPress({ url: String(href) })} style={spanStyle}>
          {content}
        </Text>
      );
    }

    return (
      context.breaks[token.type] ??
      (spanStyle ? (
        <Text key={key} style={spanStyle}>
          {content}
        </Text>
      ) : (
        content
      ))
    );
  });

/** The style of a block that is one paragraph: a heading by its level, a tight list item's text, or a paragraph. */
const paragraphStyle = (node: MarkdownNode, style: ResolvedStyle): object => {
  if (node.token.type === "heading_open") {
    return style[headingElement(Number(node.token.tag.slice(HEADING_LEVEL_OFFSET)))];
  }

  return node.token.hidden ? textOf(style.list) : style.paragraph;
};

const renderParagraph = (node: MarkdownNode, context: RenderContext): React.ReactNode => (
  <Text selectable={context.selectable} style={paragraphStyle(node, context.style)}>
    {node.children.length === NO_CHILDREN
      ? node.token.content
      : node.children.map(({ children }) => renderInline(children, context))}
  </Text>
);

const renderList = (node: MarkdownNode, context: RenderContext, renderChildren: RenderChildren): React.ReactNode => {
  const { list } = context.style;
  const start = Number(node.token.attrGet("start") ?? FIRST_ORDINAL);
  const paragraph = { ...context.style.paragraph, ...textOf(list) };
  const itemContext = { ...context, style: { ...context.style, list: { ...list, marginBottom: 0 }, paragraph } };

  return (
    <View style={{ marginBottom: list.marginBottom }}>
      {node.children.map((item, index) => (
        <View key={String(index)} style={{ flexDirection: "row" }}>
          <Text style={{ ...textOf(list), color: list.markerColor, marginRight: list.gapWidth }}>
            {node.token.type === "ordered_list_open" ? `${String(start + index)}.` : "•"}
          </Text>
          <View style={{ flexShrink: 1 }}>{renderChildren(item.children, itemContext)}</View>
        </View>
      ))}
    </View>
  );
};

const renderBlockquote = (
  node: MarkdownNode,
  context: RenderContext,
  renderChildren: RenderChildren,
): React.ReactNode => {
  const { blockquote, paragraph } = context.style;
  const quoteContext = {
    ...context,
    style: { ...context.style, paragraph: { ...paragraph, ...textOf(blockquote), marginBottom: 0 } },
  };

  return (
    <View
      style={{
        backgroundColor: blockquote.backgroundColor,
        borderLeftColor: blockquote.borderColor,
        borderLeftWidth: blockquote.borderWidth,
        marginBottom: blockquote.marginBottom,
        paddingLeft: blockquote.gapWidth,
      }}
    >
      {renderChildren(node.children, quoteContext)}
    </View>
  );
};

/** A table's rows, header and body alike, each cell one paragraph. */
const renderTable = (node: MarkdownNode, context: RenderContext): React.ReactNode => {
  const { table } = context.style;
  const cellStyle = { ...textOf(table), ...boxOf({ ...table, marginBottom: 0 }), flex: 1, padding: 8 };

  return (
    <View style={{ marginBottom: table.marginBottom }}>
      {node.children
        .flatMap((section) => section.children)
        .map((row, rowIndex) => (
          <View key={String(rowIndex)} style={{ flexDirection: "row" }}>
            {row.children.map((cell, cellIndex) => (
              <Text
                key={String(cellIndex)}
                selectable={context.selectable}
                style={cell.token.type === "th_open" ? { ...cellStyle, fontWeight: "bold" } : cellStyle}
              >
                {cell.children.map(({ children }) => renderInline(children, context))}
              </Text>
            ))}
          </View>
        ))}
    </View>
  );
};

const renderCode = (node: MarkdownNode, context: RenderContext): React.ReactNode => (
  <View style={boxOf(context.style.codeBlock)}>
    <Text selectable={context.selectable} style={textOf(context.style.codeBlock)}>
      {node.token.content.replace(/\n$/u, "")}
    </Text>
  </View>
);

/** One block; a list item or a quote renders the blocks inside it through here again. */
const renderBlock = (node: MarkdownNode, context: RenderContext): React.ReactNode => {
  const renderChildren: RenderChildren = (nodes, childContext) =>
    nodes.map((child, index) => <View key={String(index)}>{renderBlock(child, childContext)}</View>);

  switch (node.token.type) {
    case "bullet_list_open":
    case "ordered_list_open": {
      return renderList(node, context, renderChildren);
    }
    case "blockquote_open": {
      return renderBlockquote(node, context, renderChildren);
    }
    case "table_open": {
      return renderTable(node, context);
    }
    case "fence":
    case "code_block": {
      return renderCode(node, context);
    }
    case "hr": {
      const { color, height, marginBottom, marginTop } = context.style.thematicBreak;

      return <View style={{ backgroundColor: color, height, marginBottom, marginTop }} />;
    }
    case "reference_definition": {
      return null;
    }
    default: {
      return renderParagraph(node, context);
    }
  }
};

const BlockView = ({
  context,
  node,
}: {
  readonly context: RenderContext;
  readonly node: MarkdownNode;
}): React.ReactNode => renderBlock(node, context);

/**
 * One top-level block. A streamed reply only grows its last block, and `lexAppended` hands every earlier one back as
 * the same object, so those skip their render and an append costs only the block it lands in.
 */
const Block = memo(BlockView);

/** The latest `value`, read when a handler fires, so a parent's new function each render changes nothing drawn. */
const useLatest = <Value,>(value: Value): { readonly current: Value } => {
  const latest = useRef(value);

  useLayoutEffect(() => {
    latest.current = value;
  });
  return latest;
};

/** The resolved style, the same object while `markdownStyle` resolves to the same fields. */
const useResolvedStyle = (markdownStyle: MarkdownStyle): ResolvedStyle => {
  const [style, setStyle] = useState(() => resolveStyle(markdownStyle));
  const next = restyle(style, markdownStyle);

  if (next !== style) {
    setStyle(next);
  }
  return next;
};

/** The markdown, lexed by its tail as it streams in. */
const useLexed = (markdown: string): LexedMarkdown => {
  const [lexed, setLexed] = useState({ markdown: "", tokens: nothingLexed });
  const current = lexed.markdown === markdown ? lexed : { markdown, tokens: lexAppended(lexed.tokens, markdown) };

  if (current !== lexed) {
    setLexed(current);
  }
  return current.tokens;
};

/** Reports the links after a commit and only when they differ from the last report, as the library does. */
const useLinksChange = (lexed: LexedMarkdown, onLinksChange: LinksChangeHandler | null): void => {
  const handler = useLatest(onLinksChange);
  const reported = useRef("");
  const reports = onLinksChange !== null;

  useLayoutEffect(() => {
    const links = reports ? linksOf(lexed) : null;
    const key = JSON.stringify(links);

    if (links !== null && key !== reported.current) {
      handler.current?.(links);
    }
    reported.current = key;
  }, [handler, lexed, reports]);
};

/**
 * The Linux `EnrichedMarkdownText` (#639): the library's markdown drawn as React Native `Text` and `View`, so it lays
 * out and paints through the platform's own paragraph pipeline rather than a second native text stack.
 */
export const EnrichedMarkdownText = ({
  containerStyle,
  markdown,
  markdownStyle = noMarkdownStyle,
  md4cFlags,
  onLinkPress,
  onLinksChange,
  selectable = false,
}: EnrichedMarkdownTextProps): React.ReactNode => {
  const linkPress = useLatest(onLinkPress);
  const style = useResolvedStyle(markdownStyle);
  const hardSoftBreaks = md4cFlags?.hardSoftBreaks === true;
  const context = useMemo(
    () => ({
      breaks: { hardbreak: "\n", softbreak: hardSoftBreaks ? "\n" : " " },
      onLinkPress: (event: { readonly url: string }): void => linkPress.current?.(event),
      selectable,
      spans: spanStyles(style),
      style,
    }),
    [hardSoftBreaks, linkPress, selectable, style],
  );
  const lexed = useLexed(markdown);

  useLinksChange(lexed, onLinksChange ?? null);

  return (
    <View style={containerStyle}>
      {lexed.blocks.map(({ node }, index) => (
        // oxlint-disable-next-line react/no-array-index-key -- a streamed block keeps its index; blocks never reorder
        <Block context={context} key={String(index)} node={node} />
      ))}
    </View>
  );
};
