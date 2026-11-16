import { ActivityIndicator, Linking, Pressable, View } from "react-native";

import { AppText } from "./ui/AppText";
import { SymbolView } from "./ui/AppSymbol";

interface MediaVideoPlayerProps {
  readonly uri: string | null;
  readonly resolvePlaybackUri?: () => Promise<string | null>;
  readonly name: string;
  readonly unavailable?: boolean;
  readonly unavailableDetail?: string | undefined;
  readonly expanded?: boolean;
  readonly fitToVideo?: boolean;
  readonly onPlaybackStart?: () => void;
  readonly borderRadius?: number;
}

async function openVideo(props: MediaVideoPlayerProps) {
  const uri = props.resolvePlaybackUri ? await props.resolvePlaybackUri() : props.uri;
  if (uri !== null) await Linking.openURL(uri);
}

// react-native-linux: expo-video is left out of the desktop feature profile, so the poster opens
// the video in the desktop's default application instead of playing it inline.
export function MediaVideoPlayer(props: MediaVideoPlayerProps) {
  const playable = props.uri !== null && props.unavailable !== true;
  const unavailableText = props.unavailableDetail
    ? `Video unavailable. ${props.unavailableDetail}`
    : "Video unavailable";
  const frame = (
    <Pressable
      accessibilityRole="button"
      accessibilityLabel={
        props.unavailable ? `${props.name}. ${unavailableText}` : `Open ${props.name}`
      }
      accessibilityState={{ disabled: !playable }}
      disabled={!playable}
      onPress={() => {
        props.onPlaybackStart?.();
        void openVideo(props).catch(() => undefined);
      }}
      style={[
        {
          alignItems: "center",
          justifyContent: "center",
          overflow: "hidden",
          borderRadius: props.borderRadius ?? 10,
          backgroundColor: "#000000",
          paddingHorizontal: 16,
        },
        props.expanded ? { flex: 1 } : { width: "100%", maxWidth: 480, aspectRatio: 16 / 9 },
      ]}
    >
      {props.unavailable ? (
        <AppText style={{ color: "#ffffffcc", fontSize: 14, textAlign: "center" }}>
          {unavailableText}
        </AppText>
      ) : props.uri === null ? (
        <ActivityIndicator color="#ffffff" accessibilityLabel="Loading video" />
      ) : (
        <View
          style={{
            width: 48,
            height: 48,
            alignItems: "center",
            justifyContent: "center",
            borderRadius: 24,
            backgroundColor: "#00000099",
          }}
        >
          <SymbolView name="play" size={28} tintColor="#ffffff" type="monochrome" />
        </View>
      )}
      {playable ? (
        <AppText
          numberOfLines={1}
          style={{
            position: "absolute",
            right: 8,
            bottom: 8,
            maxWidth: "75%",
            borderRadius: 4,
            backgroundColor: "#00000099",
            paddingHorizontal: 8,
            paddingVertical: 4,
            color: "#ffffff",
            fontSize: 12,
          }}
        >
          {props.name}
        </AppText>
      ) : null}
    </Pressable>
  );
  if (!props.fitToVideo) return frame;
  return (
    <View style={{ flex: 1, alignItems: "center", justifyContent: "center", alignSelf: "stretch" }}>
      {frame}
    </View>
  );
}
