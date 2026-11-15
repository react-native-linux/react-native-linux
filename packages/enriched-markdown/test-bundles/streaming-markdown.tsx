// #639: a long assistant reply streamed into EnrichedMarkdownText the way T3 Code streams one. Characters arrive by
// time, as from the network, and each frame renders whatever has arrived; the message is anchored to the bottom like
// a chat feed, so every new line moves and repaints the visible message. Like T3 Code, the parent passes a new
// `onLinkPress` arrow and a new `markdownStyle` object every render, and the reply carries a 150-line code fence.
// The last section on screen when the stream ends is the golden: h3 to h6, a titled link, a table with a header
// row, a thematic break, an ordered list that starts at 3, and hard and soft breaks. This runs the Debug JS bundle;
// the same scenario on optimised Hermes is #680.
import { AppRegistry, View } from "react-native";
import { useEffect, useLayoutEffect, useRef, useState } from "react";
import { EnrichedMarkdownText } from "react-native-enriched-markdown";

const section = (index: number): string => `## Step ${String(index)}: the *streamed* reply

Markdown arrives **a few characters at a time**, with \`inline code\`, ~~struck text~~ and a [link to the docs](https://reactnative.dev).

- A bullet with **bold** and _emphasis_
- A bullet with \`useState\` and a [second link](https://example.com)
  1. A nested ordered item
  2. Another nested item

> A quote the reply cites.

\`\`\`ts
const answer = await stream(\`step ${String(index)}\`);
\`\`\`

`;

const fenceLines = 150;
const fence = `\`\`\`ts\n${Array.from({ length: fenceLines }, (_, line) => `const line${String(line)} = step(${String(line)});`).join("\n")}\n\`\`\`\n\n`;

const finale = `### Wrap-up

#### The [docs](https://reactnative.dev "Docs") cover the rest
##### A fifth-level heading
###### A sixth-level heading

| Element | Drawn |
| --- | --- |
| \`table\` | **yes** |

---

3. Third, the list starts at three
4. Fourth, with a hard break\\
   and its second line

A soft break
on its own line.
`;

const sectionCount = 4;
const sections = Array.from({ length: sectionCount }, (_, index) => section(index + 1));
const message = `# A long streamed message\n\n${sections.slice(0, 2).join("")}${fence}${sections.slice(2).join("")}${finale}`;
// A fast model's pace, about 250 tokens a second.
const charactersPerSecond = 1000;
const outputRefreshHz = 120;
// The reply must commit on at least half of the frames its stream spans, so a slow JS thread cannot pass the frame
// gate by committing less often.
const minimumCommits = Math.floor(((message.length / charactersPerSecond) * outputRefreshHz) / 2);
// The golden must not depend on a host's `monospace` alias, so code is drawn in the vendored Noto Sans.
const codeFont = "Noto Sans";

const StreamingMarkdown = (): React.JSX.Element => {
  const [length, setLength] = useState(0);
  const commits = useRef(0);
  const links = useRef(0);

  useEffect(() => {
    const startedAt = performance.now();
    let frame = 0;
    const arrive = (now: number): void => {
      const arrived = Math.min(message.length, Math.ceil(((now - startedAt) * charactersPerSecond) / 1000));

      setLength(arrived);
      if (arrived < message.length) {
        frame = requestAnimationFrame(arrive);
      }
    };

    frame = requestAnimationFrame(arrive);
    return () => cancelAnimationFrame(frame);
  }, []);

  // Logged once the whole reply is committed, not once it has arrived: a stream the renderer cannot keep up with
  // never gets here inside the scenario's frames.
  useLayoutEffect(() => {
    commits.current += 1;
    if (length === message.length) {
      console.log(
        `streaming-markdown: rendered ${String(length)} characters, ${String(links.current)} links, ` +
          `${String(commits.current)} commits (at least ${String(minimumCommits)})`,
      );
      if (commits.current >= minimumCommits) {
        console.log("streaming-markdown: kept pace");
      }
    }
  }, [length]);

  return (
    <View
      onLayout={() => console.log("streaming-markdown: committed")}
      style={{ backgroundColor: "#ffffff", flex: 1, overflow: "hidden" }}
    >
      <EnrichedMarkdownText
        // Positioned rather than flex-anchored: a flex child of a fixed-height column is measured against that height,
        // and React Native clamps a taller paragraph (the 150-line fence) to it, where a chat's scroller would not.
        containerStyle={{ bottom: 24, left: 24, position: "absolute", right: 24 }}
        markdown={message.slice(0, length)}
        markdownStyle={{
          code: { fontFamily: codeFont },
          codeBlock: { fontFamily: codeFont },
          link: { color: "#2563EB", underline: true },
          thematicBreak: { color: "#DC2626", height: 2 },
        }}
        md4cFlags={{ hardSoftBreaks: true }}
        onLinkPress={({ url }) => console.log(`streaming-markdown: pressed ${url}`)}
        onLinksChange={(reported) => {
          links.current = reported.length;
        }}
      />
    </View>
  );
};

AppRegistry.registerComponent("StreamingMarkdown", () => StreamingMarkdown);
AppRegistry.runApplication("StreamingMarkdown", { initialProps: {}, rootTag: 1 });
