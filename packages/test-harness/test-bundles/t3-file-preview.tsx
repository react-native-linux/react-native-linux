import "../../expo/test-bundles/install-whatwg-url.ts";
import { type ReactElement, useEffect, useState } from "react";
import { AppRegistry, Linking } from "react-native";
import { File, Paths } from "expo-file-system";
import { FilePreviewModal } from "../modules/t3-expo-ui/src/components/ui/FilePreviewModal.linux";
import { VideoPreviewModal } from "../modules/t3-expo-ui/src/components/ui/VideoPreviewModal.linux";

const UNOPENABLE_URL = "https://example.com/unopenable.pdf";
const SLOW_URL = "https://example.com/slow.pdf";
const SLOW_OPEN_MS = 800;
const CANCEL_AFTER_MS = 100;
const HANDOFF_PATH = /^file:.*\/t3-attachment-downloads\/\d+-[\da-f-]{36}\//u;

const report = (line: string): void => {
  console.log(`t3-file-preview: ${line}`);
};

// The composer's draft file, which it deletes once no preview holds its lease.
const draft = new File(Paths.cache, "t3-file-preview", "clip.webm");

// The native openURL spawns xdg-open, which LinkingModuleTest covers; here the URL a preview opens is what is observed.
Linking.openURL = async (url: string): Promise<void> => {
  if (url === UNOPENABLE_URL) {
    throw new Error("xdg-open failed");
  }

  if (url === SLOW_URL) {
    await new Promise((resolve) => {
      setTimeout(resolve, SLOW_OPEN_MS);
    });
    report("open slow");
    throw new Error("xdg-open failed");
  }

  if (HANDOFF_PATH.test(url)) {
    draft.delete();
    report(`open handoff copy ${url.replace(HANDOFF_PATH, "")} ${new File(url).textSync()}`);

    return;
  }

  report(`open ${url}`);
};

type Preview = (onRequestClose: () => void) => ReactElement;

const sibling = { kind: "image", name: "missing.png", uri: "https://example.com/missing.png" } as const;

const previews: readonly (readonly [string, Preview])[] = [
  ["image", (onRequestClose) => <FilePreviewModal onRequestClose={onRequestClose} source={{ gallery: [sibling, sibling], kind: "image", name: "screenshot.png", uri: "https://example.com/screenshot.png" }} />],
  ["pdf", (onRequestClose) => <FilePreviewModal onRequestClose={onRequestClose} source={{ kind: "pdf", name: "spec.pdf", uri: "https://example.com/spec.pdf" }} />],
  ["pasted image", (onRequestClose) => <FilePreviewModal onRequestClose={onRequestClose} source={{ kind: "image", name: "pasted.png", uri: "data:image/png;base64,iVBORw0KGgo=" }} />],
  ["protocol-relative image", (onRequestClose) => <FilePreviewModal onRequestClose={onRequestClose} source={{ kind: "image", name: "relative.png", uri: "//example.com/relative.png" }} />],
  ["unopenable", (onRequestClose) => <FilePreviewModal onRequestClose={onRequestClose} source={{ kind: "document", name: "unopenable.pdf", uri: UNOPENABLE_URL }} />],
  ["canceled", (onRequestClose) => <FilePreviewModal onOpenError={() => report("open error canceled")} onRequestClose={onRequestClose} source={{ kind: "document", name: "slow.pdf", uri: SLOW_URL }} />],
  ["media video", (onRequestClose) => <VideoPreviewModal onRequestClose={onRequestClose} source={{ mimeType: "video/mp4", name: "demo.mp4", type: "media", uri: "https://example.com/demo.mp4" }} />],
  ["local video", (onRequestClose) => <VideoPreviewModal onRequestClose={onRequestClose} source={{ attachment: { fileUri: draft.uri, id: "clip", mimeType: "video/webm", name: "clip.webm", sizeBytes: 4, type: "file" }, type: "local" }} />],
];

// Each preview mounts once the previous one asked to close, so the trace shows every open and close in order. The
// canceled one is unmounted before its open settles, as when the screen under it loses focus.
const T3FilePreview = (): ReactElement | null => {
  const [index, setIndex] = useState(0);
  const current = previews[index];

  useEffect(() => {
    if (current === undefined) {
      report("done");
    } else if (current[0] === "canceled") {
      setTimeout(() => {
        report("unmount canceled");
        setIndex(index + 1);
      }, CANCEL_AFTER_MS);
    }
  }, [current, index]);

  return current === undefined
    ? null
    : current[1](() => {
        report(`closed ${current[0]}`);
        setIndex(index + 1);
      });
};

draft.create({ intermediates: true, overwrite: true });
draft.writeSync("clip");
AppRegistry.registerComponent("T3FilePreview", () => T3FilePreview);
AppRegistry.runApplication("T3FilePreview", { initialProps: {}, rootTag: 1 });
