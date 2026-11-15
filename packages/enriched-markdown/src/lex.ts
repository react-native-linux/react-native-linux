import MarkdownIt from "markdown-it";

type Token = ReturnType<typeof markdownIt.parse>[number];
type MappedToken = Token & { readonly map: readonly [number, number] };

/** A token with what sits between it and its closing token, so a renderer walks a tree rather than a flat stream. */
interface MarkdownNode {
  readonly children: readonly MarkdownNode[];
  readonly token: Token;
}

/** One top-level block and the source lines it was lexed from, which are what say whether it changed. */
interface MarkdownBlock {
  readonly node: MarkdownNode;
  readonly source: string;
}

const OPENING = 1;
const CLOSING = -1;
const FIRST = 0;
const NEXT = 1;
const LINE_BREAK = "\n";
const markdownIt = new MarkdownIt("default", { linkify: true });

/** Consumes `tokens` up to the closing token of the node they are the children of, an inline token's own included. */
const nestFrom = (tokens: Iterator<Token>): MarkdownNode[] => {
  const nodes: MarkdownNode[] = [];

  for (let next = tokens.next(); next.done !== true && next.value.nesting !== CLOSING; next = tokens.next()) {
    const token = next.value;
    const children = token.nesting === OPENING ? nestFrom(tokens) : nestFrom((token.children ?? [])[Symbol.iterator]());

    nodes.push({ children, token });
  }

  return nodes;
};

const isMapped = (node: MarkdownNode): node is MarkdownNode & { readonly token: MappedToken } =>
  node.token.map !== null;

/**
 * Lexes `source` into top-level blocks. Each block owns the lines from the one it starts on to the one the next
 * block starts on, and the first block owns any blank lines before it, so the blocks' sources joined by line breaks
 * are `source` again.
 */
const lex = (source: string): MarkdownBlock[] => {
  const lines = source.split(LINE_BREAK);
  const nodes = nestFrom(markdownIt.parse(source, {})[Symbol.iterator]()).filter((node) => isMapped(node));
  const starts = nodes.map(({ token }) => token.map[FIRST]);

  starts[FIRST] = FIRST;

  return nodes.map((node, index) => ({
    node,
    source: lines.slice(starts[index], starts[index + NEXT]).join(LINE_BREAK),
  }));
};

interface LexedMarkdown {
  readonly blocks: readonly MarkdownBlock[];
}

/**
 * Lexes a streamed reply by its tail. Every top-level block before the last is final once text arrives after it, so
 * those blocks are kept as they were, objects and all, and only the source from the start of the last block on is
 * lexed again: a list, a fence or a paragraph that is still growing is re-lexed whole, and nothing before it is. A
 * source that does not extend the kept blocks is lexed from scratch.
 */
const lexAppended = (previous: LexedMarkdown, source: string): LexedMarkdown => {
  const kept = previous.blocks.slice(FIRST, CLOSING);
  const keptSource = kept.map((block) => block.source + LINE_BREAK).join("");

  return source.startsWith(keptSource)
    ? { blocks: [...kept, ...lex(source.slice(keptSource.length))] }
    : { blocks: lex(source) };
};

const nothingLexed: LexedMarkdown = { blocks: [] };

export { lexAppended, nothingLexed };
