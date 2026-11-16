import * as Dialog from "./Dialog";
import type { ComponentType } from "react";

import type { FilePreviewModalProps, FilePreviewViewerProps } from "./FilePreviewModal.types";

// The test bundle's stand-in: T3 Code's session resolves attachments and asset URLs first, and
// here a source carries its URI or a local attachment's file URI. As there, a document's open
// error gets the session's alert, and an image's goes to the viewer.
export function FilePreviewSession(
  props: FilePreviewModalProps & {
    readonly progressiveGallery: boolean;
    readonly Viewer: ComponentType<FilePreviewViewerProps>;
  },
) {
  const { source, Viewer } = props;
  if (source === null) return null;
  console.log(`FilePreviewSession gallery ${String(source.gallery?.length)}`);
  const uri = "uri" in source ? source.uri : "attachment" in source ? source.attachment.fileUri : "";
  return source.kind === "image" ? (
    <Viewer
      source={{ kind: "image", images: [{ ...source, uri }], selectedIndex: 0 }}
      onRequestClose={props.onRequestClose}
    />
  ) : (
    <Viewer
      source={{ ...source, kind: source.kind, uri }}
      onRequestClose={props.onRequestClose}
      onOpenError={(error) =>
        props.onOpenError
          ? props.onOpenError(error)
          : Dialog.alert(
              "Could not open preview",
              "The file could not be opened. Check the connection and try again.",
            )
      }
    />
  );
}
