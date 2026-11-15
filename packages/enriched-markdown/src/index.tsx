import { Text, View } from "react-native";
import { boxOf, headingElement, noMarkdownStyle, resolveStyle, textOf } from "./styles.ts";
import { lexAppended, nothingLexed } from "./lex.ts";
import { memo, useMemo, useState } from "react";

type MarkdownStyle = typeof noMarkdownStyle;
type ResolvedStyle = ReturnType<typeof resolveStyle>;
type MarkdownNode = ReturnType<typeof lexAppended>["blocks"][number]["node"];
type LinkPressHandler = (event: { readonly url: string }) => void;

interface EnrichedMarkdownTextProps {
  readonly containerStyle?: React.ComponentProps<typeof View>["style"];
  readonly markdown: string;
  readonly markdownStyle?: MarkdownStyle;
  readonly onLinkPress?: LinkPressHandler;
  readonly selectable?: boolean;
}

interface RenderContext {
  readonly onLinkPress: LinkPressHandler | null;
  readonly selectable: boolean;
  readonly spans: ReturnType<typeof spanStyles>;
  readonly style: ResolvedStyle;
}

type RenderChildren = (nodes: readonly MarkdownNode[], context: RenderContext) => React.ReactNode;

const NO_CHILDREN = 0;
const FIRST_ORDINAL = 1;
const HEADING_LEVEL_OFFSET = "h".length;
const breaks: Readonly<Record<string, string>> = { hardbreak: "\n", softbreak: " " };

/** The span style of each inline token that opens one, read from the resolved markdown style. */
const spanStyles = ({ code, em, link, strikethrough, strong }: ResolvedStyle): Readonly<Record<string, object>> => ({
  code_inline: { ...textOf(code), backgroundColor: code.backgroundColor },
  em_open: textOf(em),
  link_open: {
    color: link.color,
    fontFamily: link.fontFamily,
    textDecorationLine: link.underline === false ? "none" : "underline",
  },
  s_open: { color: strikethrough.color, textDecorationLine: "line-through" },
  strong_open: textOf(strong),
});

const renderInline = (nodes: readonly MarkdownNode[], context: RenderContext): React.ReactNode =>
  nodes.map(({ children, token }, index) => {
    const key = String(index);
    const content = children.length === NO_CHILDREN ? token.content : renderInline(children, context);
    const spanStyle = context.spans[token.type];
    const href = token.attrGet("href");

    if (href !== null) {
      return (
        <Text key={key} onPress={(): void => context.onLinkPress?.({ url: String(href) })} style={spanStyle}>
          {content}
        </Text>
      );
    }

    return (
      breaks[token.type] ??
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
  const itemContext = { ...context, style: { ...context.style, list: { ...list, marginBottom: 0 } } };

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
      return <View style={context.style.thematicBreak} />;
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

/**
 * The Linux `EnrichedMarkdownText` (#639): the library's markdown drawn as React Native `Text` and `View`, so it lays
 * out and paints through the platform's own paragraph pipeline rather than a second native text stack.
 */
export const EnrichedMarkdownText = ({
  containerStyle,
  markdown,
  markdownStyle = noMarkdownStyle,
  onLinkPress,
  selectable = false,
}: EnrichedMarkdownTextProps): React.ReactNode => {
  const context = useMemo(() => {
    const style = resolveStyle(markdownStyle);

    return { onLinkPress: onLinkPress ?? null, selectable, spans: spanStyles(style), style };
  }, [markdownStyle, onLinkPress, selectable]);
  const [lexed, setLexed] = useState({ markdown: "", tokens: nothingLexed });
  const current = lexed.markdown === markdown ? lexed : { markdown, tokens: lexAppended(lexed.tokens, markdown) };

  if (current !== lexed) {
    setLexed(current);
  }

  return (
    <View style={containerStyle}>
      {current.tokens.blocks.map(({ node }, index) => (
        // oxlint-disable-next-line react/no-array-index-key -- a streamed block keeps its index; blocks never reorder
        <Block context={context} key={String(index)} node={node} />
      ))}
    </View>
  );
};
