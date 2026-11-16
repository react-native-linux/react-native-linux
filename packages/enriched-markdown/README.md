# @react-native-linux/enriched-markdown

The Linux `EnrichedMarkdownText` of https://github.com/software-mansion-labs/react-native-enriched-markdown, the
streaming markdown view T3 Code renders every message with. A `linux` bundle resolves `react-native-enriched-markdown`
to this package when it is installed (`packages/cli/src/package-aliases.ts`).

## Why a reimplementation, not an overlay

ADR-0002 reaches a library by vendoring it and overlaying its platform files. That needs a part of the library
Linux can run. The library has two: a native component (md4c parsing into Objective-C and Kotlin text views) and,
for the web, md4c compiled to WebAssembly rendering into the DOM. Hermes has no WebAssembly and Linux has no DOM, so
neither runs here (#639 has the measurements).

This package is therefore its own, small implementation of the library's props: markdown-it parses, and the
markdown is drawn as React Native `Text` and `View`, so it lays out and paints through the platform's paragraph
pipeline like any other text.

- `src/lex.ts`: lexes a streamed reply by its tail. Blocks before the last two keep their objects, so their memoised
  renders are skipped and an append costs only the blocks it touches.
- `src/styles.ts`: the library's default style for each element, with `markdownStyle` laid over it.
- `src/index.tsx`: the component.

## Parity

| Prop or slot | Linux | Notes |
| --- | --- | --- |
| `markdown` | yes | Lexed by its tail while it streams. |
| `markdownStyle` | partly | Headings, paragraph, emphasis, strong, strikethrough, inline code, code block, link, list, blockquote, table and thematic break. Fields beyond the ones `styles.ts` reads are ignored. |
| `containerStyle` | yes | |
| `selectable` | yes | Per paragraph. |
| `onLinkPress` | yes | Read when the link is pressed, so a new function each render costs nothing. |
| `onLinksChange` | yes | Reported after a commit, only when the links differ. |
| `md4cFlags.hardSoftBreaks` | yes | |
| other `md4cFlags` | no | `latexMath`, `admonitions`, `preserveWindowsPaths` and the rest are ignored. |
| `renderers` (code and image block slots) | no | #688 |
| images, `resolveImageSource`, `onImagePress` | no | #688 |
| task lists, `onTaskListItemPress` | no | #688 |
| `flavor` | no | #688. Parsing is CommonMark with tables, strikethrough and autolinks. |
| `onLinkLongPress`, link context menus, link variants and pills | no | |
| LaTeX, admonitions, spoilers | no | |

Code uses the `monospace` generic, which fontconfig resolves.
