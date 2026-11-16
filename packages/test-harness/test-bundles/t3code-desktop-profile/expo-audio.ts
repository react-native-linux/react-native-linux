import { Linking } from "react-native";
import { useSyncExternalStore } from "react";

type AudioSource = string | { readonly uri: string } | null;

interface AudioStatus {
  readonly currentTime: number;
  readonly didJustFinish: boolean;
  readonly duration: number;
  readonly error: string | null;
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
  readonly subscribe: (listener: () => void) => () => void;
}

const deniedPermission = { canAskAgain: false, expires: "never", granted: false, status: "denied" } as const;

const uriOf = (source: AudioSource): string | null => (typeof source === "string" ? source : (source?.uri ?? null));

const settled = async (): Promise<void> => {
  await Promise.resolve();
};

/**
 * The desktop feature profile's `expo-audio` (docs/research/t3code-linux.md §2): its native module, ExpoAudio, is
 * left out. A player's `play` opens its source in the desktop's default application, which owns playback from there:
 * the player reports playing, then finished once the hand-off resolves, or the error when it rejects, so T3 Code's own
 * finish and retry paths run. Linux has no shared audio session to configure.
 */
const createAudioPlayer = (initialSource: AudioSource): AudioPlayer => {
  let source = initialSource;
  let status: AudioStatus = {
    currentTime: 0,
    didJustFinish: false,
    duration: 0,
    error: null,
    isBuffering: false,
    isLoaded: uriOf(source) !== null,
    playing: false,
  };
  const listeners = new Set<() => void>();
  const publish = (change: Partial<AudioStatus>): void => {
    status = { ...status, ...change };
    for (const listener of listeners) {
      listener();
    }
  };
  const setSource = (next: AudioSource): void => {
    source = next;
    publish({ isLoaded: uriOf(next) !== null });
  };

  return {
    get currentStatus(): AudioStatus {
      return status;
    },
    pause: (): void => undefined,
    play: (): void => {
      const uri = uriOf(source);

      if (uri === null) {
        return;
      }

      publish({ didJustFinish: false, error: null, playing: true });
      void Linking.openURL(uri)
        .then(
          (): Partial<AudioStatus> => ({ didJustFinish: true, playing: false }),
          (error: unknown): Partial<AudioStatus> => ({
            error: error instanceof Error ? error.message : String(error),
            playing: false,
          }),
        )
        .then((change) => {
          setTimeout(() => {
            publish(change);
          }, 0);
        });
    },
    release: (): void => {
      setSource(null);
    },
    replace: setSource,
    seekTo: settled,
    subscribe: (listener: () => void): (() => void) => {
      listeners.add(listener);

      return (): void => {
        listeners.delete(listener);
      };
    },
  };
};

const useAudioPlayerStatus = (player: AudioPlayer): AudioStatus =>
  useSyncExternalStore(player.subscribe, () => player.currentStatus);

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
