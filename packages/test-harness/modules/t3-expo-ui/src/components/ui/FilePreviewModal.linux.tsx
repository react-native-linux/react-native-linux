import * as Dialog from "./Dialog";
import { useEffect, useEffectEvent } from "react";
import { Linking } from "react-native";

import { uuidv4 } from "../../lib/uuid";
import type { FilePreviewModalProps, FilePreviewViewerProps } from "./FilePreviewModal.types";
import { FilePreviewSession } from "./FilePreviewSession";

export type {
  FilePreviewItem,
  FilePreviewSource,
  ResolvedFilePreviewSource,
} from "./FilePreviewModal.types";

// lib/attachmentDownload.ts sweeps this directory's day-old `<time>-<uuid>` entries.
const HANDOFF_DIRECTORY = "t3-attachment-downloads";
const OPENABLE_URI = /^(file|https?):/i;

// The session's lease on a local file ends when the preview closes, and the composer may then
// delete it, so the default application gets a cached copy, as Android's openAttachmentInViewer does.
async function handOff(uri: string): Promise<string> {
  if (!/^file:/i.test(uri)) return uri;
  const { Directory, File, Paths } = await import("expo-file-system");
  const original = new File(uri);
  const directory = new Directory(Paths.cache, HANDOFF_DIRECTORY, `${Date.now()}-${uuidv4()}`);
  directory.create({ intermediates: true });
  const copy = new File(directory, original.name);
  await original.copy(copy);
  return copy.uri;
}

async function openInDefaultApplication(uri: string | undefined) {
  if (uri === undefined || !OPENABLE_URI.test(uri)) throw new Error("No file to open");
  await Linking.openURL(await handOff(uri));
}

// react-native-linux: no Modal host (#62) or in-app viewer yet, so the resolved file opens in the
// desktop's default application, as Android's DocumentPreview hands documents to ACTION_VIEW.
function LinuxFilePreview(props: FilePreviewViewerProps) {
  const { source } = props;
  const uri = source.kind === "image" ? source.images[source.selectedIndex]?.uri : source.uri;
  const onRequestClose = useEffectEvent(props.onRequestClose);
  const onOpenError = useEffectEvent((error: unknown) => {
    if (props.onOpenError) props.onOpenError(error);
    else Dialog.alert("Could not open preview", "The file could not be opened. Please try again.");
  });
  // oxlint-disable-next-line t3code/no-unreasoned-state-sync -- hands the file to the default application once per presentation
  useEffect(() => {
    let canceled = false;
    void openInDefaultApplication(uri)
      .catch((error: unknown) => {
        if (!canceled) onOpenError(error);
      })
      .finally(() => {
        if (!canceled) onRequestClose();
      });
    return () => {
      canceled = true;
    };
  }, [uri]);
  return null;
}

/** Opens the tapped image, PDF or document in the desktop's default application. */
export function FilePreviewModal(props: FilePreviewModalProps) {
  return (
    <FilePreviewSession
      {...props}
      source={props.source && { ...props.source, gallery: [] }}
      progressiveGallery={false}
      Viewer={LinuxFilePreview}
    />
  );
}
