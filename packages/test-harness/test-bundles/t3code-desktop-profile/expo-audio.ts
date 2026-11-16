import { Linking } from "react-native";

type AudioSource = string | { readonly uri: string } | null;

interface AudioStatus {
  readonly currentTime: number;
  readonly didJustFinish: boolean;
  readonly duration: number;
  readonly isBuffering: boolean;
  readonly isLoaded: boolean;
  readonly playing: boolean;
}

interface AudioPlayer {
  readonly currentStatus: AudioStatus;
  readonly pause: () => void;
  readonly play: () => void;
  readonly release: () => void;
  readonly replace: (source: AudioSource) => void;
  readonly seekTo: () => Promise<void>;
}

const deniedPermission = { canAskAgain: false, expires: "never", granted: false, status: "denied" } as const;

const uriOf = (source: AudioSource): string | null => (typeof source === "string" ? source : (source?.uri ?? null));

const settled = async (): Promise<void> => {
  await Promise.resolve();
};

/**
 * The desktop feature profile's `expo-audio` (docs/research/t3code-linux.md §2): its native module, ExpoAudio, is
 * left out. A player's `play` opens its source in the desktop's default application, which owns playback from there,
 * so the player itself never plays and Linux has no shared audio session to configure.
 */
const createAudioPlayer = (initialSource: AudioSource): AudioPlayer => {
  let source = initialSource;

  return {
    get currentStatus(): AudioStatus {
      return {
        currentTime: 0,
        didJustFinish: false,
        duration: 0,
        isBuffering: false,
        isLoaded: uriOf(source) !== null,
        playing: false,
      };
    },
    pause: (): void => undefined,
    play: (): void => {
      const uri = uriOf(source);

      if (uri !== null) {
        Linking.openURL(uri).catch((error: unknown) => {
          console.error("[expo-audio]", error);
        });
      }
    },
    release: (): void => {
      source = null;
    },
    replace: (next: AudioSource): void => {
      source = next;
    },
    seekTo: settled,
  };
};

const useAudioPlayerStatus = (player: AudioPlayer): AudioStatus => player.currentStatus;

/** Voice input is left out: recording permission reads as denied, and a recorder created anyway throws. */
class AudioRecorder {
  public constructor() {
    throw new Error("expo-audio: recording is left out of the Linux desktop profile");
  }
}

const AudioModule = { AudioRecorder };

const RecordingPresets = {
  HIGH_QUALITY: { android: {}, bitRate: 128_000, extension: ".m4a", ios: {}, numberOfChannels: 2, sampleRate: 44_100 },
};

const getRecordingPermissionsAsync = async (): Promise<typeof deniedPermission> =>
  await Promise.resolve(deniedPermission);

export {
  AudioModule,
  RecordingPresets,
  createAudioPlayer,
  getRecordingPermissionsAsync,
  getRecordingPermissionsAsync as requestRecordingPermissionsAsync,
  settled as setAudioModeAsync,
  settled as setIsAudioActiveAsync,
  useAudioPlayerStatus,
};
