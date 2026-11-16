import { useEffect, useState } from "react";
import { AppRegistry, View } from "react-native";
import { Accelerometer } from "./t3code-desktop-profile/expo-sensors";

const SHAKE_INTERVAL_MS = 50;
const SETTLE_MS = 200;

const report = (line: string): void => {
  console.log(`t3-expo-sensors: ${line}`);
};

/** DevicePreviewRouteScreen's shake effect: it sets the interval, listens while focused and removes on cleanup. */
const useShakeToggle = (): void => {
  useEffect(() => {
    Accelerometer.setUpdateInterval(SHAKE_INTERVAL_MS);
    const subscription = Accelerometer.addListener((sample) => {
      report(`sample ${sample.x} ${sample.y} ${sample.z}`);
    });
    report("subscribed");

    return () => {
      subscription.remove();
      report("removed");
    };
  }, []);
};

const ShakeListener = (): null => {
  useShakeToggle();

  return null;
};

const T3ExpoSensors = (): React.JSX.Element => {
  const [isFocused, setIsFocused] = useState(true);

  useEffect(() => {
    Accelerometer.isAvailableAsync().then(
      (available) => {
        report(`available ${String(available)}`);
      },
      (error: unknown) => {
        report(`failed ${String(error)}`);
      },
    );
    const timer = setTimeout(() => {
      setIsFocused(false);
      setTimeout(() => {
        report("ready");
      }, SETTLE_MS);
    }, SETTLE_MS);

    return () => {
      clearTimeout(timer);
    };
  }, []);

  return <View style={{ flex: 1 }}>{isFocused ? <ShakeListener /> : null}</View>;
};

AppRegistry.registerComponent("T3ExpoSensors", () => T3ExpoSensors);
AppRegistry.runApplication("T3ExpoSensors", { initialProps: {}, rootTag: 1 });
