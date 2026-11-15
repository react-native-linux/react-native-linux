const markdownElements = [
  "blockquote",
  "code",
  "codeBlock",
  "em",
  "h1",
  "h2",
  "h3",
  "h4",
  "h5",
  "h6",
  "link",
  "list",
  "paragraph",
  "strikethrough",
  "strong",
  "table",
  "thematicBreak",
] as const;

type MarkdownElement = (typeof markdownElements)[number];

/**
 * The subset of the library's per-element style fields this renderer reads, in React Native's units. A field left
 * `undefined` is unset, so a span inherits it from the paragraph around it.
 */
interface BlockStyle {
  readonly backgroundColor?: string | undefined;
  readonly borderColor?: string | undefined;
  readonly borderRadius?: number | undefined;
  readonly borderWidth?: number | undefined;
  readonly color?: string | undefined;
  readonly fontFamily?: string | undefined;
  readonly fontSize?: number | undefined;
  readonly fontStyle?: "italic" | "normal" | undefined;
  readonly fontWeight?: "bold" | "normal" | undefined;
  readonly gapWidth?: number | undefined;
  readonly height?: number | undefined;
  readonly lineHeight?: number | undefined;
  readonly marginBottom?: number | undefined;
  readonly marginTop?: number | undefined;
  readonly markerColor?: string | undefined;
  readonly padding?: number | undefined;
  readonly underline?: boolean | undefined;
}

type MarkdownStyle = Partial<Record<MarkdownElement, BlockStyle>>;
type ResolvedStyle = Readonly<Record<MarkdownElement, BlockStyle>>;

const monospace = "monospace";
const bodyText = { color: "#1F2937", fontSize: 16, lineHeight: 26, marginBottom: 16 };
const headingText = { color: "#111827", fontWeight: "bold", marginBottom: 8 } as const;

/** The library's own defaults (`normalizeMarkdownStyle.ts`) for the elements this renderer draws. */
const defaultStyle: ResolvedStyle = {
  blockquote: {
    ...bodyText,
    backgroundColor: "#F9FAFB",
    borderColor: "#D1D5DB",
    borderWidth: 3,
    color: "#4B5563",
    gapWidth: 16,
  },
  code: { backgroundColor: "#FDF2F4", color: "#E01E5A", fontFamily: monospace },
  codeBlock: {
    backgroundColor: "#1F2937",
    borderColor: "#374151",
    borderRadius: 8,
    borderWidth: 1,
    color: "#F3F4F6",
    fontFamily: monospace,
    fontSize: 14,
    lineHeight: 22,
    marginBottom: 16,
    padding: 16,
  },
  em: { fontStyle: "italic" },
  h1: { ...headingText, fontSize: 30, lineHeight: 38 },
  h2: { ...headingText, fontSize: 24, lineHeight: 32 },
  h3: { ...headingText, fontSize: 20, lineHeight: 28 },
  h4: { ...headingText, fontSize: 18, lineHeight: 26 },
  h5: { ...headingText, color: "#374151", fontSize: 16, lineHeight: 24 },
  h6: { ...headingText, color: "#4B5563", fontSize: 14, lineHeight: 22 },
  link: { color: "#2563EB", underline: true },
  list: { ...bodyText, gapWidth: 12, markerColor: "#6B7280" },
  paragraph: bodyText,
  strikethrough: { color: "#9CA3AF" },
  strong: { fontWeight: "bold" },
  table: { ...bodyText, borderColor: "#E5E7EB", borderWidth: 1, fontSize: 14, lineHeight: 22 },
  thematicBreak: { backgroundColor: "#E5E7EB", height: 1, marginBottom: 24, marginTop: 24 },
};

/** Shared, so an omitted `markdownStyle` never invalidates the memoised blocks on the next append. */
const noMarkdownStyle: MarkdownStyle = {};

const headingElements = ["h1", "h2", "h3", "h4", "h5", "h6"] as const;
const FIRST_HEADING_DEPTH = 1;

const resolveStyle = (markdownStyle: MarkdownStyle): ResolvedStyle => {
  const resolved = { ...defaultStyle };

  for (const element of markdownElements) {
    resolved[element] = { ...defaultStyle[element], ...markdownStyle[element] };
  }

  return resolved;
};

/** A heading's element by its ATX depth; markdown has six levels, so anything deeper reads as the last. */
const headingElement = (depth: number): MarkdownElement => headingElements[depth - FIRST_HEADING_DEPTH] ?? "h6";

/** The text fields alone, for a span or for the text inside a block whose box takes the rest. */
const textOf = ({ color, fontFamily, fontSize, fontStyle, fontWeight, lineHeight }: BlockStyle): BlockStyle => ({
  color,
  fontFamily,
  fontSize,
  fontStyle,
  fontWeight,
  lineHeight,
});

/** The box fields alone, for a block's `View`. */
const boxOf = ({
  backgroundColor,
  borderColor,
  borderRadius,
  borderWidth,
  marginBottom,
  marginTop,
  padding,
}: BlockStyle): BlockStyle => ({
  backgroundColor,
  borderColor,
  borderRadius,
  borderWidth,
  marginBottom,
  marginTop,
  padding,
});

export { boxOf, headingElement, noMarkdownStyle, resolveStyle, textOf };
