import { describe, expect, it, vi } from "vitest";
import { lexAppended, linksOf, nothingLexed } from "./lex.ts";
import MarkdownIt from "markdown-it";

const START = 0;
const ONE_CHARACTER = 1;
const THREE_ITEMS = 3;

const reply = [
  "",
  "# Title",
  "",
  "A [reference used before it is defined][later] and [one used after][earlier].",
  "",
  "[earlier]: https://example.com/earlier",
  "",
  "A *streamed* paragraph with `code`, ~~strike~~ and a [link](https://reactnative.dev).",
  "",
  "- one",
  "- two",
  "",
  "- three, after a blank line",
  "  1. nested",
  "",
  "1. First",
  "",
  "2. Second, in the same loose list",
  "",
  "3. Third",
  "",
  "> a quote with [the earlier reference][earlier]",
  ">",
  "> [quoted]: https://example.com/quoted",
  "",
  "```ts",
  "const a = 1;",
  "```",
  "",
  "| a | b |",
  "| - | - |",
  "| 1 | 2 |",
  "",
  "---",
  "[Quoted][quoted], [earlier] and [later] after their definitions.",
  "",
  "[later]: https://example.com/later 'Later'",
  "The end.",
].join("\n");

/** Streams `reply` in chunks of `size` characters, the way a model's reply arrives, each step checked by `check`. */
const stream = (
  size: number,
  check?: (lexed: ReturnType<typeof lexAppended>, source: string) => void,
): ReturnType<typeof lexAppended> => {
  let lexed = lexAppended(nothingLexed, "");

  for (let length = size; length < reply.length + size; length += size) {
    const source = reply.slice(START, length);

    lexed = lexAppended(lexed, source);
    check?.(lexed, source);
  }

  return lexed;
};

const shapeOf = (lexed: ReturnType<typeof lexAppended>): string =>
  JSON.stringify(
    lexed.blocks.map(({ node }) => node),
    (key, value: unknown) => (key === "map" ? null : value),
  );

describe("linksOf", () => {
  it("reports every link once streamed, with its label, title and target", () => {
    expect(linksOf(stream(ONE_CHARACTER))).toStrictEqual([
      { label: "reference used before it is defined", title: "Later", url: "https://example.com/later" },
      { label: "one used after", title: "", url: "https://example.com/earlier" },
      { label: "link", title: "", url: "https://reactnative.dev" },
      { label: "the earlier reference", title: "", url: "https://example.com/earlier" },
      { label: "Quoted", title: "", url: "https://example.com/quoted" },
      { label: "earlier", title: "", url: "https://example.com/earlier" },
      { label: "later", title: "Later", url: "https://example.com/later" },
    ]);
  });

  it("reads the label of a link with emphasis inside it", () => {
    expect(linksOf(lexAppended(nothingLexed, "[**bold** text](https://example.com)"))).toStrictEqual([
      { label: "bold text", title: "", url: "https://example.com" },
    ]);
  });
});

describe("lexAppended", () => {
  it("lexes a reply streamed a character at a time the same as a full parse at every step", () => {
    const streamed = stream(ONE_CHARACTER, (lexed, source) => {
      expect(shapeOf(lexed), source).toBe(shapeOf(lexAppended(nothingLexed, source)));
    });

    expect(streamed.blocks.map(({ source }) => source).join("\n")).toBe(reply);
  });

  it("keeps a loose ordered list one list while its items stream in", () => {
    let lexed = nothingLexed;

    for (const source of ["1. First\n\n2", "1. First\n\n2. Second\n\n3", "1. First\n\n2. Second\n\n3. Third"]) {
      lexed = lexAppended(lexed, source);
    }

    expect(lexed.blocks.map(({ node }) => [node.token.type, node.children.length])).toStrictEqual([
      ["ordered_list_open", THREE_ITEMS],
    ]);
  });

  it("nests each opening token's children up to its closing token", () => {
    const [list] = lexAppended(nothingLexed, "- **one**").blocks;

    expect(list?.node.token.type).toBe("bullet_list_open");
    expect(list?.node.children.map(({ token }) => token.type)).toStrictEqual(["list_item_open"]);
    expect(
      list?.node.children[START]?.children[START]?.children[START]?.children.map(({ token }) => token.type),
    ).toStrictEqual(["text", "strong_open", "text"]);
  });
});

describe("lexAppended's block objects", () => {
  it("keeps the earlier blocks as the same objects", () => {
    const first = lexAppended(nothingLexed, "# Title\n\nA paragraph\n\nAnother para");

    expect(lexAppended(first, "# Title\n\nA paragraph\n\nAnother paragraph").blocks[START]).toBe(first.blocks[START]);
  });

  it("hands back a block lexed again from the same source as the same object", () => {
    const first = lexAppended(nothingLexed, "# Title\n\nA paragraph");

    expect(lexAppended(first, "# Title\n\nA paragraph\n\nMore").blocks[START]).toBe(first.blocks[START]);
  });

  it("lexes only the tail once a reference above it is defined", () => {
    const first = lexAppended(nothingLexed, "[x]: https://example.com\n\nA\n\nB\n\nC");
    const parse = vi.spyOn(MarkdownIt.prototype, "parse");

    lexAppended(first, "[x]: https://example.com\n\nA\n\nB\n\nC [x]");
    expect(parse.mock.calls.map(([source]) => source)).toStrictEqual(["B\n\nC [x]"]);
    parse.mockRestore();
  });

  it("re-renders only the blocks whose links a new reference definition changes", () => {
    const first = lexAppended(nothingLexed, "A\n\nB [x]\n\nC");
    const defined = lexAppended(first, "A\n\nB [x]\n\nC\n\n[x]: https://example.com");

    expect(defined.blocks[START]).toBe(first.blocks[START]);
    expect(defined.blocks[ONE_CHARACTER]).not.toBe(first.blocks[ONE_CHARACTER]);
  });

  it("lexes from scratch when the source no longer extends the kept blocks", () => {
    const lexed = lexAppended(lexAppended(nothingLexed, "# Title\n\nA paragraph\n\nAnother"), "Replaced");

    expect(lexed.blocks.map(({ node }) => node.token.type)).toStrictEqual(["paragraph_open"]);
  });
});
