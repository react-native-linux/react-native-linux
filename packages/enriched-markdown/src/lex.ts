import MarkdownIt from "markdown-it";

type Token = ReturnType<typeof markdownIt.parse>[number];
type MappedToken = Token & { readonly map: readonly [number, number] };

/** A token with what sits between it and its closing token, so a renderer walks a tree rather than a flat stream. */
interface MarkdownNode {
  readonly children: readonly MarkdownNode[];
  readonly token: Token;
}

/**
 * One top-level block, the source lines it was lexed from, which are what say whether it changed, and the labels of
 * the link reference definitions inside it.
 */
interface MarkdownBlock {
  readonly labels: readonly string[];
  readonly node: MarkdownNode;
  readonly source: string;
}

type References = Readonly<Record<string, { readonly href: string; readonly title: string }>>;

/** The lexed blocks and every link reference they define, by normalised label, as markdown-it's `env` holds them. */
interface LexedMarkdown {
  readonly blocks: readonly MarkdownBlock[];
  readonly references: References;
}

/** A link as the library's `onLinksChange` reports it. */
interface MarkdownLinkOccurrence {
  readonly label: string;
  readonly title: string;
  readonly url: string;
}

const OPENING = 1;
const CLOSING = -1;
const FIRST = 0;
const STILL_OPEN = -2;
const NEXT = 1;
const LINE_BREAK = "\n";
// Kept, so a reference definition is a block of its own with the lines it was defined on.
const markdownIt = new MarkdownIt("default", { linkify: true }).disable("strip_references");

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

/** The labels of the reference definitions among `nodes` and the blocks inside them. */
const labelsOf = (nodes: readonly MarkdownNode[]): string[] =>
  nodes.flatMap(({ children, token }) => {
    if (token.type === "reference_definition") {
      return [String(token.meta?.["label"])];
    }

    return token.type === "inline" ? [] : labelsOf(children);
  });

/**
 * Lexes `source` into top-level blocks, with `references` defined before it. Each block owns the lines from the one
 * it starts on to the one the next block starts on, and the first block owns any blank lines before it, so the
 * blocks' sources joined by line breaks are `source` again.
 */
const lex = (source: string, references: References): LexedMarkdown => {
  const lines = source.split(LINE_BREAK);
  const env = { references: { ...references } };
  const nodes = nestFrom(markdownIt.parse(source, env)[Symbol.iterator]()).filter((node) => isMapped(node));
  const starts = nodes.map(({ token }) => token.map[FIRST]);

  starts[FIRST] = FIRST;

  return {
    blocks: nodes.map((node, index) => ({
      labels: labelsOf([node]),
      node,
      source: lines.slice(starts[index], starts[index + NEXT]).join(LINE_BREAK),
    })),
    references: env.references,
  };
};

/** The text a link's children draw, for its label. */
const plainText = (nodes: readonly MarkdownNode[]): string =>
  nodes.map(({ children, token }) => token.content + plainText(children)).join("");

const linksIn = (nodes: readonly MarkdownNode[]): MarkdownLinkOccurrence[] =>
  nodes.flatMap(({ children, token }) =>
    token.type === "link_open"
      ? [
          {
            label: plainText(children),
            title: String(token.attrGet("title") ?? ""),
            url: String(token.attrGet("href")),
          },
        ]
      : linksIn(children),
  );

const linksByBlock = new WeakMap<MarkdownNode, readonly MarkdownLinkOccurrence[]>();

/** A block's links, read once per block object. */
const linksOfBlock = ({ node }: MarkdownBlock): readonly MarkdownLinkOccurrence[] => {
  const links = linksByBlock.get(node) ?? linksIn([node]);

  linksByBlock.set(node, links);
  return links;
};

/** The links in `lexed`, in reading order; a kept block's are read once, so a streamed append reads only the tail. */
const linksOf = ({ blocks }: LexedMarkdown): MarkdownLinkOccurrence[] => blocks.flatMap((block) => linksOfBlock(block));

/**
 * The block lexed before at the same place when it was lexed from the same source and resolves the same links: the
 * links are all a reference definition changes, so a memoised render of it is still right.
 */
const reused = (before: MarkdownBlock | undefined, block: MarkdownBlock): MarkdownBlock =>
  before?.source === block.source && JSON.stringify(linksOfBlock(before)) === JSON.stringify(linksOfBlock(block))
    ? before
    : block;

/**
 * Lexes a streamed reply by its tail. Text arriving after a block can still join it to the one before (a loose list
 * gains its next item across a blank line), so every top-level block but the last two is kept as it was, objects
 * and all, and the source from the start of the second-to-last block on is lexed again, with the references the
 * kept blocks define. A reference definition that is new or still growing can change a link above it, so it lexes
 * the whole reply, as does a source that does not extend the kept blocks. Either way a block lexed again to the same
 * result is handed back as the object it was.
 */
const lexAppended = (previous: LexedMarkdown, source: string): LexedMarkdown => {
  const kept = previous.blocks.slice(FIRST, STILL_OPEN);
  const keptSource = kept.map((block) => block.source + LINE_BREAK).join("");
  const keptLabels = new Set(kept.flatMap((block) => block.labels));
  const keptReferences = Object.fromEntries(
    Object.entries(previous.references).filter(([label]) => keptLabels.has(label)),
  );
  const tail = source.startsWith(keptSource) ? lex(source.slice(keptSource.length), keptReferences) : null;
  const extendsKept = tail !== null && JSON.stringify(tail.references) === JSON.stringify(previous.references);
  const { blocks, references } = extendsKept ? tail : lex(source, {});
  const start = extendsKept ? kept.length : FIRST;

  return {
    blocks: [
      ...kept.slice(FIRST, start),
      ...blocks.map((block, index) => reused(previous.blocks[start + index], block)),
    ],
    references,
  };
};

const nothingLexed: LexedMarkdown = { blocks: [], references: {} };

export { lexAppended, linksOf, nothingLexed };
