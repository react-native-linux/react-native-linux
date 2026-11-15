import { Directory, File, Paths } from "expo-file-system";
import { AppRegistry, View } from "react-native";
import { URL } from "whatwg-url-minimum";
import { useEffect } from "react";

// `expo`'s runtime installs this URL in an app; expo-file-system's `Paths.join` needs it, not React Native's.
Object.defineProperty(globalThis, "URL", { value: URL, configurable: true, writable: true });

const fromDataHome = (uri: string): string => uri.slice(uri.indexOf("/data/"));
const fromCacheHome = (uri: string): string => uri.slice(uri.indexOf("/cache/"));

const writeAtomically = async (file: File, contents: string): Promise<void> => {
  const temporary = new File(file.parentDirectory, `${file.name}.1.tmp`);

  temporary.create({ intermediates: true, overwrite: true });
  await temporary.write(contents);
  temporary.moveSync(file, { overwrite: true });
};

const exercise = async (): Promise<string> => {
  const outbox = new Directory(Paths.document, "thread outbox");

  outbox.create({ idempotent: true, intermediates: true });
  const message = new File(outbox, "message-1.json");

  await message.write(JSON.stringify({ id: 1 }));
  await writeAtomically(message, JSON.stringify({ id: 2 }));
  const image = new File(outbox, "image.bin");

  await image.write("aGk=", { encoding: "base64" });
  new Directory(outbox, "nested").create();
  const listed = outbox
    .list()
    .map((entry) => `${entry instanceof File ? "file" : "directory"}:${entry.name}`)
    .join(",");
  const marker = new File(Paths.document, "voice-audio-grant-id");

  marker.writeSync("grant");
  const copy = new File(Paths.cache, "copy.bin");

  await image.copy(copy);
  const fields = [
    `document=${fromDataHome(Paths.document.uri)}`,
    `cache=${fromCacheHome(Paths.cache.uri)}`,
    `message=${message.uri.slice(message.uri.indexOf("thread"))}`,
    `listed=${listed}`,
    `text=${await message.text()}`,
    `bytes=${String([...(await image.bytes())])}`,
    `base64=${await copy.base64()}`,
    `size=${String(image.size)}`,
    `marker=${marker.textSync()}`,
    `parent=${String(message.parentDirectory.uri === outbox.uri)}`,
  ];

  outbox.delete();
  marker.delete();
  copy.delete();
  fields.push(`deleted=${String(!outbox.exists && !marker.exists && !copy.exists)}`);

  return fields.join(" ");
};

const ExpoFileSystemApp = (): React.JSX.Element => {
  useEffect(() => {
    exercise().then(
      (result) => {
        console.log(`expo-file-system: ${result}`);
      },
      (error: unknown) => {
        console.log(`expo-file-system: FAILED ${String(error)}`);
      },
    );
  }, []);

  return <View />;
};

AppRegistry.registerComponent("ExpoFileSystem", () => ExpoFileSystemApp);
AppRegistry.runApplication("ExpoFileSystem", { initialProps: {}, rootTag: 1 });
