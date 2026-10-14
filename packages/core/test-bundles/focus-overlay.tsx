import { useEffect, useRef, useState } from "react";
import { AppRegistry, Pressable, View } from "react-native";

type ControlProps = { readonly name: string; readonly onPress?: () => void; readonly ref?: React.Ref<View> };

const Control = ({ name, onPress, ref }: ControlProps): React.JSX.Element => (
  <Pressable
    onBlur={() => console.log(`focus-overlay: blur ${name}`)}
    onFocus={() => console.log(`focus-overlay: focus ${name}`)}
    onPress={() => {
      console.log(`focus-overlay: press ${name}`);
      onPress?.();
    }}
    ref={ref}
    style={{ backgroundColor: "#3b82f6", height: 60, marginBottom: 10 }}
  />
);

const Overlay = ({ onClose }: { readonly onClose: () => void }): React.JSX.Element => {
  const inside = useRef<View>(null);

  useEffect(() => {
    inside.current?.focus();
  }, []);

  return (
    <View style={{ backgroundColor: "#0f172a", inset: 0, padding: 80, position: "absolute" }}>
      <Control name="inside" ref={inside} />
      <Control name="close" onPress={onClose} />
    </View>
  );
};

const App = (): React.JSX.Element => {
  const invoker = useRef<View>(null);
  const [isOpen, setIsOpen] = useState(false);

  useEffect(() => {
    console.log("focus-overlay: committed");
  }, []);

  return (
    <View style={{ flex: 1, padding: 40 }}>
      <Control name="open" onPress={() => setIsOpen(true)} ref={invoker} />
      <Control name="next" />
      {isOpen && (
        <Overlay
          onClose={() => {
            setIsOpen(false);
            invoker.current?.focus();
          }}
        />
      )}
    </View>
  );
};

AppRegistry.registerComponent("FocusOverlay", () => App);
AppRegistry.runApplication("FocusOverlay", { initialProps: {}, rootTag: 1 });
