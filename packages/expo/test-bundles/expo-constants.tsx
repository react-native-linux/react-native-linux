import Constants from "expo-constants";
import { EventEmitter } from "expo-modules-core";
import { AppRegistry, View } from "react-native";
import { requiredConstantsFields } from "../src/expo-constants-surface.ts";
import { useEffect } from "react";

const UUID_PATTERN = /^[\da-f]{8}-[\da-f]{4}-4[\da-f]{3}-[89ab][\da-f]{3}-[\da-f]{12}$/u;

const describeField = (field: string): string => {
  const value: unknown = Constants[field];

  if (typeof value === "function") {
    return "function";
  }

  return field === "sessionId" && UUID_PATTERN.test(String(value)) ? "uuid" : (JSON.stringify(value) ?? "undefined");
};

const roundTripEvent = (): string => {
  const emitter = new EventEmitter<{ ping: (value: string) => void }>();
  let received = "nothing";

  emitter.addListener("ping", (value) => {
    received = value;
  });
  emitter.emit("ping", "pong");

  return received;
};

const ExpoConstantsApp = (): React.JSX.Element => {
  useEffect(() => {
    void Constants.getWebViewUserAgentAsync().then((userAgent) => {
      const fields = requiredConstantsFields.map((field) => `${field}=${describeField(field)}`).join(" ");
      const hasDeviceName = typeof Constants.deviceName === "string" && Constants.deviceName.length > 0;

      console.log(
        `expo-constants: ${fields} deviceName=${String(hasDeviceName)} userAgent=${String(userAgent)} ` +
          `eventEmitter=${roundTripEvent()}`,
      );
    });
  }, []);

  return <View />;
};

AppRegistry.registerComponent("ExpoConstants", () => ExpoConstantsApp);
AppRegistry.runApplication("ExpoConstants", { initialProps: {}, rootTag: 1 });
