import { boxOf, headingElement, resolveStyle, textOf } from "./styles.ts";
import { describe, expect, it } from "vitest";

const FIRST_DEPTH = 1;
const THIRD_DEPTH = 3;
const BEYOND_LAST_DEPTH = 7;

describe("resolveStyle", () => {
  it("answers the library's defaults when no style is given", () => {
    expect(resolveStyle({}).h1).toStrictEqual({
      color: "#111827",
      fontSize: 30,
      fontWeight: "bold",
      lineHeight: 38,
      marginBottom: 8,
    });
  });

  it("lays a given field over the default and keeps the rest", () => {
    const style = resolveStyle({ paragraph: { color: "#000000" } });

    expect(style.paragraph).toStrictEqual({ color: "#000000", fontSize: 16, lineHeight: 26, marginBottom: 16 });
    expect(style.link).toStrictEqual({ color: "#2563EB", underline: true });
  });
});

describe("headingElement", () => {
  it("maps each ATX depth to its element and anything deeper to the last", () => {
    expect(headingElement(FIRST_DEPTH)).toBe("h1");
    expect(headingElement(THIRD_DEPTH)).toBe("h3");
    expect(headingElement(BEYOND_LAST_DEPTH)).toBe("h6");
  });
});

describe("textOf and boxOf", () => {
  const { codeBlock } = resolveStyle({});

  it("splits a block's text fields from its box fields", () => {
    expect(textOf(codeBlock)).toEqual({
      color: "#F3F4F6",
      fontFamily: "monospace",
      fontSize: 14,
      lineHeight: 22,
    });
    expect(boxOf(codeBlock)).toEqual({
      backgroundColor: "#1F2937",
      borderColor: "#374151",
      borderRadius: 8,
      borderWidth: 1,
      marginBottom: 16,
      padding: 16,
    });
  });
});
