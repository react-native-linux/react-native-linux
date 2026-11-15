import { useRef, useState } from "react";
import { AppRegistry, Text, View } from "react-native";
import { ComposerEditor } from "../modules/t3-composer-editor/src/native/T3ComposerEditor";

const MINIMUM_HEIGHT = 48;
const VERTICAL_INSET = 24;

type EnterBehavior = "send" | "newline";

interface Selection {
  readonly start: number;
  readonly end: number;
}

const report = (line: string): void => {
  console.log(`t3-composer-editor: ${line.replaceAll("\n", String.raw`\n`)}`);
};

const Composer = ({
  enterBehavior,
  name = enterBehavior,
  controlsSelection = true,
  onSent,
}: {
  readonly enterBehavior: EnterBehavior;
  readonly name?: string;
  readonly controlsSelection?: boolean;
  readonly onSent: (line: string) => void;
}): React.JSX.Element => {
  const [value, setValue] = useState("");
  const draft = useRef("");
  const [selection, setSelection] = useState<Selection>({ end: 0, start: 0 });
  const [height, setHeight] = useState(0);

  return (
    <ComposerEditor
      contentInsetVertical={VERTICAL_INSET / 2}
      enterBehavior={enterBehavior}
      onChangeText={(text) => {
        report(`${name} text="${text}"`);
        draft.current = text;
        setValue(text);
      }}
      onContentSizeChange={(size) => {
        if (height > 0 && size.height > height) {
          report(`${name} grew`);
        }
        setHeight(size.height);
      }}
      onSelectionChange={setSelection}
      onSubmit={(alternate) => {
        report(`${name} submit alternate=${String(alternate)} text="${draft.current}"`);
        onSent(`${alternate ? "Alternate" : "Sent"}: ${draft.current}`);
        draft.current = "";
        setValue("");
        setSelection({ end: 0, start: 0 });
      }}
      placeholder={`Ask anything (Return: ${enterBehavior})`}
      {...(controlsSelection ? { selection } : {})}
      style={{
        backgroundColor: "#1e293b",
        borderRadius: 18,
        height: Math.max(MINIMUM_HEIGHT, height + VERTICAL_INSET),
        marginTop: 16,
        paddingHorizontal: 16,
      }}
      textStyle={{ color: "#f8fafc" }}
      value={value}
    />
  );
};

const ComposerApp = (): React.JSX.Element => {
  const [sent, setSent] = useState("Nothing sent");

  return (
    <View onLayout={() => report("ready")} style={{ backgroundColor: "#0f172a", flex: 1, padding: 24, width: 480 }}>
      <Composer enterBehavior="send" onSent={setSent} />
      <Composer enterBehavior="newline" onSent={setSent} />
      <Composer controlsSelection={false} enterBehavior="send" name="plain" onSent={setSent} />
      <Text style={{ color: "#94a3b8", fontSize: 14, marginTop: 16 }}>{sent}</Text>
    </View>
  );
};

AppRegistry.registerComponent("T3ComposerEditor", () => ComposerApp);
AppRegistry.runApplication("T3ComposerEditor", { initialProps: {}, rootTag: 1 });
