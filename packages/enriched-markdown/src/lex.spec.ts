import { describe, expect, it } from "vitest";
import { lexAppended, nothingLexed } from "./lex.ts";

const START = 0;
const ONE_CHARACTER = 1;

const reply = [
  "",
  "# Title",
  "",
  "A *streamed* paragraph with `code`, ~~strike~~ and a [link](https://reactnative.dev).",
  "",
  "- one",
  "- two",
  "",
  "- three, after a blank line",
  "  1. nested",
  "",
  "> a quote",
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
  "The end.",
].join("\n");

/** Streams `reply` in chunks of `size` characters, the way a model's reply arrives. */
const stream = (size: number): ReturnType<typeof lexAppended> => {
  let lexed = lexAppended(nothingLexed, "");

  for (let length = size; length < reply.length + size; length += size) {
    lexed = lexAppended(lexed, reply.slice(START, length));
  }

  return lexed;
};

const shapeOf = (lexed: ReturnType<typeof lexAppended>): string =>
  JSON.stringify(
    lexed.blocks.map(({ node }) => node),
    (key, value: unknown) => (key === "map" ? null : value),
  );

describe("lexAppended", () => {
  it("lexes a reply streamed a character at a time the same as the whole reply at once", () => {
    const streamed = stream(ONE_CHARACTER);

    expect(shapeOf(streamed)).toBe(shapeOf(lexAppended(nothingLexed, reply)));
    expect(streamed.blocks.map(({ source }) => source).join("\n")).toBe(reply);
  });

  it("nests each opening token's children up to its closing token", () => {
    const [list] = lexAppended(nothingLexed, "- **one**").blocks;

    expect(list?.node.token.type).toBe("bullet_list_open");
    expect(list?.node.children.map(({ token }) => token.type)).toStrictEqual(["list_item_open"]);
    expect(
      list?.node.children[START]?.children[START]?.children[START]?.children.map(({ token }) => token.type),
    ).toStrictEqual(["text", "strong_open", "text"]);
  });

  it("keeps the earlier blocks as the same objects", () => {
    const first = lexAppended(nothingLexed, "# Title\n\nA para");

    expect(lexAppended(first, "# Title\n\nA paragraph").blocks[START]).toBe(first.blocks[START]);
  });

  it("lexes from scratch when the source no longer extends the kept blocks", () => {
    const lexed = lexAppended(lexAppended(nothingLexed, "# Title\n\nA para"), "Replaced");

    expect(lexed.blocks.map(({ node }) => node.token.type)).toStrictEqual(["paragraph_open"]);
  });
});
