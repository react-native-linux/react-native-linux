import { useEffect } from "react";
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

type Player = ReturnType<typeof createAudioPlayer>;

const SETTLE_MS = 200;
const MISSING_URL = "https://example.com/missing.ogg";

const report = (line: string): void => {
  console.log(`t3-expo-audio: ${line}`);
};

// The native openURL spawns xdg-open, which LinkingModuleTest covers; here the URL a player opens is what is observed.
Linking.openURL = async (url: string): Promise<void> => {
  report(`open ${url}`);

  if (url === MISSING_URL) {
    throw new Error(`no application opens ${url}`);
  }
};

const phases: Record<string, string> = {};

const settle = async (): Promise<void> => {
  await new Promise((resolve) => {
    setTimeout(resolve, SETTLE_MS);
  });
};

/** What T3 Code's previews render from: the status `useAudioPlayerStatus` re-renders them with. */
const Probe = ({ name, player }: { readonly name: string; readonly player: Player }): null => {
  const { didJustFinish, error, isLoaded, playing } = useAudioPlayerStatus(player);

  useEffect(() => {
    report(
      `${name} ${phases[name] ?? "start"} loaded=${String(isLoaded)} playing=${String(playing)} finished=${String(didJustFinish)} error=${error ?? "none"}`,
    );
  }, [name, didJustFinish, error, isLoaded, playing]);

  return null;
};

const players = {
  failing: createAudioPlayer(MISSING_URL),
  file: createAudioPlayer("https://example.com/voice-note.m4a"),
  inline: createAudioPlayer(null),
};

const step = async (name: keyof typeof players, phase: string, action: (player: Player) => void): Promise<void> => {
  phases[name] = phase;
  action(players[name]);
  await settle();
};

/** AudioFilePreview's player, played twice: the second play has to clear the first one's finish. */
const playFile = async (): Promise<void> => {
  await players.file.seekTo();
  await step("file", "first", (player) => {
    player.play();
  });
  await step("file", "second", (player) => {
    player.play();
  });
  await step("file", "released", (player) => {
    player.pause();
    player.release();
  });
};

/** ThreadInlineFilePreview's player: created empty, given its source on the first play. */
const playInline = async (): Promise<void> => {
  await step("inline", "empty", (player) => {
    player.play();
  });
  await step("inline", "replaced", (player) => {
    player.replace({ uri: "https://example.com/inline.ogg" });
  });
  await step("inline", "played", (player) => {
    player.play();
  });
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
  await playInline();
  await step("failing", "played", (player) => {
    player.play();
  });
  await record();
  report("ready");
};

const T3ExpoAudio = (): React.JSX.Element => {
  useEffect(() => {
    run().catch((error: unknown) => {
      report(`failed ${String(error)}`);
    });
  }, []);

  return (
    <View style={{ flex: 1 }}>
      {Object.entries(players).map(([name, player]) => (
        <Probe key={name} name={name} player={player} />
      ))}
    </View>
  );
};

AppRegistry.registerComponent("T3ExpoAudio", () => T3ExpoAudio);
AppRegistry.runApplication("T3ExpoAudio", { initialProps: {}, rootTag: 1 });
