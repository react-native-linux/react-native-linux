import type { VideoPreviewSource } from "../../lib/videoPreviewSource";
import { FilePreviewModal } from "./FilePreviewModal";

export type { VideoPreviewSource } from "../../lib/videoPreviewSource";

// react-native-linux: expo-video and AVKit's fullscreen player are not on Linux, so the video opens
// in the desktop's default application through the same session as every other file preview.
export function VideoPreviewModal(props: {
  readonly source: VideoPreviewSource | null;
  readonly onRequestClose: () => void;
}) {
  return (
    <FilePreviewModal
      source={props.source && { ...props.source, kind: "document" }}
      onRequestClose={props.onRequestClose}
    />
  );
}
