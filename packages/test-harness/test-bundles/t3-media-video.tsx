import { type ComponentProps, useEffect } from "react";
import { AppRegistry, Linking, View } from "react-native";
import { MediaVideoPlayer } from "../modules/t3-expo-ui/src/components/MediaVideoPlayer.linux";

const TILE_WIDTH = 320;
const SPACING = 16;

const report = (line: string): void => {
  console.log(`t3-media-video: ${line}`);
};

// The native openURL spawns xdg-open, which LinkingModuleTest covers; here the URL the poster opens is what is observed.
Linking.openURL = async (url: string): Promise<void> => {
  report(`open ${url}`);
};

const tiles: readonly ComponentProps<typeof MediaVideoPlayer>[] = [
  {
    name: "demo.mp4",
    onPlaybackStart: () => {
      report("demo playback start");
    },
    uri: "https://example.com/demo.mp4",
  },
  {
    name: "signed-recording.webm",
    resolvePlaybackUri: async () => "https://assets.example.com/signed-recording.webm?signature=renewed",
    uri: "https://assets.example.com/signed-recording.webm?signature=expired",
  },
  {
    name: "missing.mp4",
    onPlaybackStart: () => {
      report("missing playback start");
    },
    unavailable: true,
    unavailableDetail: "Reconnect to load it.",
    uri: "https://example.com/missing.mp4",
  },
];

const T3MediaVideo = (): React.JSX.Element => {
  useEffect(() => {
    report("ready");
  }, []);

  return (
    <View style={{ alignItems: "flex-start", backgroundColor: "#808080", columnGap: SPACING, flex: 1, flexDirection: "row", padding: SPACING }}>
      {tiles.map((tile) => (
        <View key={tile.name} style={{ width: TILE_WIDTH }}>
          <MediaVideoPlayer {...tile} />
        </View>
      ))}
    </View>
  );
};

AppRegistry.registerComponent("T3MediaVideo", () => T3MediaVideo);
AppRegistry.runApplication("T3MediaVideo", { initialProps: {}, rootTag: 1 });
