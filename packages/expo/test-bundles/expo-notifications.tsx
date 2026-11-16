import * as Notifications from "expo-notifications";
import { AppRegistry, View } from "react-native";
import { useEffect } from "react";

const SHOW: Notifications.NotificationBehavior = {
  shouldPlaySound: true,
  shouldSetBadge: false,
  shouldShowBanner: true,
  shouldShowList: true,
};

const errorCode = async (probe: () => Promise<unknown>): Promise<string> => {
  try {
    return `resolved ${JSON.stringify(await probe())}`;
  } catch (error) {
    return error instanceof Error && "code" in error ? String(error.code) : String(error);
  }
};

const nextResponse = async (): Promise<Notifications.NotificationResponse> =>
  new Promise((resolve) => {
    const subscription = Notifications.addNotificationResponseReceivedListener((response) => {
      subscription.remove();
      resolve(response);
    });
  });

// What T3 Code's agent-awareness feature calls at startup and from its settings, then a turn-complete alert that the
// e2e's daemon clicks as soon as it shows.
const exercise = async (): Promise<string> => {
  Notifications.setNotificationHandler({ handleNotification: async () => Promise.resolve(SHOW) });
  Notifications.addPushTokenListener(() => undefined).remove();

  const before = await Notifications.getLastNotificationResponseAsync();
  const permissions = await Notifications.getPermissionsAsync();
  const requested = await Notifications.requestPermissionsAsync({
    ios: { allowAlert: true, allowBadge: true, allowSound: true },
  });
  const response = nextResponse();
  const identifier = await Notifications.scheduleNotificationAsync({
    content: { body: "Thread 4 finished", data: { url: "t3code://thread/4" }, title: "Turn complete" },
    identifier: "turn-4",
    trigger: null,
  });
  const { actionIdentifier, notification } = await response;
  const last = await Notifications.getLastNotificationResponseAsync();

  await Notifications.clearLastNotificationResponseAsync();
  await Notifications.dismissAllNotificationsAsync();

  return [
    `before=${JSON.stringify(before)}`,
    `permissions=${permissions.status},${requested.status}`,
    `push=${await errorCode(Notifications.getDevicePushTokenAsync)}`,
    `badge=${await errorCode(Notifications.getBadgeCountAsync)}`,
    `scheduled=${identifier}`,
    `response=${actionIdentifier === Notifications.DEFAULT_ACTION_IDENTIFIER ? "default" : actionIdentifier}`,
    `url=${JSON.stringify(notification.request.content.data["url"])}`,
    `last=${last?.notification.request.identifier ?? "none"}`,
    `cleared=${JSON.stringify(await Notifications.getLastNotificationResponseAsync())}`,
  ].join(" ");
};

const ExpoNotificationsApp = (): React.JSX.Element => {
  useEffect(() => {
    void exercise().then(
      (line) => {
        console.log(`expo-notifications: ${line}`);
      },
      (error: unknown) => {
        console.log(`expo-notifications: FAILED ${String(error)}`);
      },
    );
  }, []);

  return <View style={{ backgroundColor: "#ffffff", flex: 1 }} />;
};

AppRegistry.registerComponent("main", () => ExpoNotificationsApp);
AppRegistry.runApplication("main", { initialProps: {}, rootTag: 1 });
