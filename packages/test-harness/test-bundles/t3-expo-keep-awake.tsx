import { useEffect, useState } from "react";
import { AppRegistry, View } from "react-native";
import {
  ExpoKeepAwakeTag,
  activateKeepAwakeAsync,
  deactivateKeepAwake,
  useKeepAwake,
} from "./t3code-desktop-profile/expo-keep-awake";

const SETTLE_MS = 200;
const VOICE_TAG = "t3-voice-input";

const report = (line: string): void => {
  console.log(`t3-expo-keep-awake: ${line}`);
};

/** VoiceInputProvider's keepScreenAwake, which its session calls as recording starts and releases as it stops. */
const keepScreenAwake = (tag: string): (() => void) => {
  const activation = activateKeepAwakeAsync(tag);
  activation.then(
    () => {
      report(`activated ${tag}`);
    },
    (error: unknown) => {
      report(`failed activate ${String(error)}`);
    },
  );

  return () => {
    activation
      .then(async () => {
        await deactivateKeepAwake(tag);
        report(`deactivated ${tag}`);
      })
      .catch((error: unknown) => {
        report(`failed deactivate ${String(error)}`);
      });
  };
};

/** Expo's development withDevTools, which wraps the root and calls the hook with Expo's default tag. */
const DevToolsRoot = (): null => {
  useKeepAwake();

  useEffect(() => {
    report(`dev tools mounted ${ExpoKeepAwakeTag}`);

    return () => {
      report("dev tools unmounted");
    };
  }, []);

  return null;
};

const T3ExpoKeepAwake = (): React.JSX.Element => {
  const [isDevToolsMounted, setIsDevToolsMounted] = useState(true);

  useEffect(() => {
    const release = keepScreenAwake(VOICE_TAG);
    const timer = setTimeout(() => {
      release();
      setIsDevToolsMounted(false);
      setTimeout(() => {
        report("ready");
      }, SETTLE_MS);
    }, SETTLE_MS);

    return () => {
      clearTimeout(timer);
    };
  }, []);

  return <View style={{ flex: 1 }}>{isDevToolsMounted ? <DevToolsRoot /> : null}</View>;
};

AppRegistry.registerComponent("T3ExpoKeepAwake", () => T3ExpoKeepAwake);
AppRegistry.runApplication("T3ExpoKeepAwake", { initialProps: {}, rootTag: 1 });
