// ADR-0005 (#794): React Native's own <Modal>, transparent, whose child View is the scrim, over a list of pressable
// rows that keeps scrolling beneath it one step per frame. The modal is declared inside a clipped, transformed,
// zIndex -1 parent, as the goldens' is.
//
// `modal-scrim-press` clicks the scrim over a row, which must press nothing, and then the card, which must press
// the card. `modal-scrim-over-list-120hz` holds the same picture to the 120 Hz frame budget.
import { useEffect, useRef } from "react";
import { AppRegistry, Modal, Pressable, ScrollView, View } from "react-native";

const rowCount = 60;
const rowHeight = 48;
const scrollStep = 3;
const framesPerReport = 240;

const log = (line: string): void => {
  console.log(`modal-scrim: ${line}`);
};

const Row = ({ index }: { readonly index: number }): React.JSX.Element => (
  <Pressable
    onPress={() => log(`row ${String(index)} press`)}
    style={{ backgroundColor: index % 2 === 0 ? "#98c379" : "#61afef", height: rowHeight - 8, margin: 4 }}
  />
);

const useScrollingEveryFrame = (list: React.RefObject<ScrollView | null>): void => {
  useEffect(() => {
    let frame = 0;
    let request = 0;

    const step = (): void => {
      frame += 1;
      list.current?.scrollTo({ animated: false, y: (frame * scrollStep) % (rowCount * rowHeight) });

      if (frame % framesPerReport === 0) {
        log(`scrolled ${String(frame)} frames`);
      }

      request = requestAnimationFrame(step);
    };

    request = requestAnimationFrame(step);

    return () => cancelAnimationFrame(request);
  }, [list]);
};

const App = (): React.JSX.Element => {
  const list = useRef<ScrollView>(null);

  useScrollingEveryFrame(list);
  useEffect(() => log("committed"), []);

  return (
    <View style={{ backgroundColor: "#1e2430", flex: 1 }}>
      <ScrollView ref={list} style={{ flex: 1 }}>
        {Array.from({ length: rowCount }, (_, index) => (
          <Row index={index} key={index} />
        ))}
      </ScrollView>
      <View
        style={{
          height: 120,
          left: 120,
          opacity: 0.5,
          overflow: "hidden",
          position: "absolute",
          top: 140,
          transform: [{ translateX: 30 }, { rotate: "12deg" }, { scale: 0.6 }],
          width: 180,
          zIndex: -1,
        }}
      >
        <Modal animationType="none" onRequestClose={() => log("request close")} transparent visible>
          <View style={{ backgroundColor: "rgba(0, 0, 0, 0.5)", inset: 0, position: "absolute" }} />
          <Pressable
            onPress={() => log("card press")}
            style={{ backgroundColor: "#f5f5f5", borderRadius: 16, height: 160, left: 200, top: 200, width: 300 }}
          />
        </Modal>
      </View>
    </View>
  );
};

AppRegistry.registerComponent("ModalScrimOverList", () => App);
AppRegistry.runApplication("ModalScrimOverList", { initialProps: {}, rootTag: 1 });
