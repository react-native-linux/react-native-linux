import { AppRegistry, Linking, View } from "react-native";
import {
  AudioModule,
  RecordingPresets,
  createAudioPlayer,
  getRecordingPermissionsAsync,
  requestRecordingPermissionsAsync,
  setAudioModeAsync,
  setIsAudioActiveAsync,
  useAudioPlayerStatus,
} from "./t3code-desktop-profile/expo-audio";

const report = (line: string): void => {
  console.log(`t3-expo-audio: ${line}`);
};

// The native openURL spawns xdg-open, which LinkingModuleTest covers; here the URL a player opens is what is observed.
Linking.openURL = async (url: string): Promise<void> => {
  report(`open ${url}`);
};

const describeStatus = (player: ReturnType<typeof createAudioPlayer>): string => {
  const { isLoaded, playing } = useAudioPlayerStatus(player);

  return `loaded=${String(isLoaded)} playing=${String(playing)}`;
};

/** AudioFilePreview's player: created with its file, played after a seek to the start. */
const playFile = async (): Promise<void> => {
  const player = createAudioPlayer("https://example.com/voice-note.m4a");

  report(`file ${describeStatus(player)}`);
  await player.seekTo();
  player.play();
  player.pause();
  player.release();
  report(`file released ${describeStatus(player)}`);
};

/** ThreadInlineFilePreview's player: created empty, given its source on the first play. */
const playInline = (): void => {
  const player = createAudioPlayer(null);

  report(`inline ${describeStatus(player)}`);
  player.play();
  player.replace({ uri: "https://example.com/inline.ogg" });
  report(`inline replaced ${describeStatus(player)}`);
  player.play();
};

/** VoiceInputProvider's recorder and permission, which voice input reaches only if a transcriber is available. */
const record = async (): Promise<void> => {
  const permissions = [await getRecordingPermissionsAsync(), await requestRecordingPermissionsAsync()];

  report(`permission ${permissions.map(({ granted, status }) => `${status}/${String(granted)}`).join(" ")}`);
  report(`preset ${RecordingPresets.HIGH_QUALITY.extension}`);

  try {
    report(`recorder ${String(new AudioModule.AudioRecorder())}`);
  } catch (error: unknown) {
    report(`recorder threw ${error instanceof Error ? error.message : String(error)}`);
  }
};

const run = async (): Promise<void> => {
  await setAudioModeAsync();
  await setIsAudioActiveAsync();
  await playFile();
  playInline();
  await record();
  report("ready");
};

const T3ExpoAudio = (): React.JSX.Element => <View style={{ flex: 1 }} />;

AppRegistry.registerComponent("T3ExpoAudio", () => T3ExpoAudio);
AppRegistry.runApplication("T3ExpoAudio", { initialProps: {}, rootTag: 1 });
run().catch((error: unknown) => {
  report(`failed ${String(error)}`);
});
