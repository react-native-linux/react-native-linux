// #639: a long assistant reply streamed into EnrichedMarkdownText the way T3 Code streams one. Characters arrive by
// time, as from the network, and each frame renders whatever has arrived; the view is anchored to the bottom like a
// chat feed, so every new line moves and repaints the visible message. The last section on screen when the stream
// ends is the golden: headings, emphasis, inline code, strikethrough, lists, links, a quote and a fence.
import { AppRegistry, View } from "react-native";
import { useEffect, useLayoutEffect, useState } from "react";
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

const sectionCount = 12;
const message = `# A long streamed message\n\n${Array.from({ length: sectionCount }, (_, index) => section(index + 1)).join("")}`;
// A fast model's pace, about 250 tokens a second: the stream lasts about four seconds.
const charactersPerSecond = 1000;
// The golden must not depend on a host's `monospace` alias, so code is drawn in the vendored Noto Sans.
const markdownStyle = { code: { fontFamily: "Noto Sans" }, codeBlock: { fontFamily: "Noto Sans" } };

const StreamingMarkdown = (): React.JSX.Element => {
  const [length, setLength] = useState(0);

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
    if (length === message.length) {
      console.log(`streaming-markdown: rendered ${String(length)} characters`);
    }
  }, [length]);

  return (
    <View
      onLayout={() => console.log("streaming-markdown: committed")}
      style={{ backgroundColor: "#ffffff", flex: 1, justifyContent: "flex-end", overflow: "hidden", padding: 24 }}
    >
      <EnrichedMarkdownText markdown={message.slice(0, length)} markdownStyle={markdownStyle} />
    </View>
  );
};

AppRegistry.registerComponent("StreamingMarkdown", () => StreamingMarkdown);
AppRegistry.runApplication("StreamingMarkdown", { initialProps: {}, rootTag: 1 });
