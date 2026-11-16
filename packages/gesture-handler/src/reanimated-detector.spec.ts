import { expect, it, vi } from "vitest";

type Callable = (...callArguments: unknown[]) => unknown;

const runNow = vi.hoisted(
  () =>
    (worklet: Callable, ...workletArguments: unknown[]): unknown =>
      worklet(...workletArguments),
);

vi.mock("react-native", () => ({ Platform: { OS: "linux" } }));
vi.mock("react", async (importOriginal) => ({
  ...(await importOriginal<object>()),
  useEffect: vi.fn(),
  useMemo: (create: Callable): unknown => create(),
  useRef: (current: unknown): unknown => ({ current }),
}));
vi.mock("../upstream/packages/react-native-gesture-handler/src/handlers/gestures/reanimatedWrapper.ts", () => ({
  Reanimated: { useEvent: (worklet: unknown): unknown => ({ workletEventHandler: { worklet } }) },
  Worklets: {
    UIRuntimeId: 1,
    createShareable: (_runtime: unknown, initial: unknown): unknown => ({ value: initial }),
    isWorkletFunction: (): boolean => true,
    scheduleOnUI: runNow,
  },
}));
vi.mock("../upstream/packages/react-native-gesture-handler/src/RNGestureHandlerModule.ts", () => ({ default: {} }));
vi.mock("../upstream/packages/react-native-gesture-handler/src/v3/detectors/HostGestureDetector.tsx", () => ({
  default: "HostGestureDetector",
}));

const sourceDirectory = "../upstream/packages/react-native-gesture-handler/src";
const detectorPath = `${sourceDirectory}/v3/detectors/ReanimatedNativeDetector.linux.tsx`;
const eventHandlerHookPath = `${sourceDirectory}/v3/hooks/callbacks/useReanimatedEventHandler.ts`;
const stateBegan = 2;
const stateActive = 4;
const firstPanTag = 1;
const secondPanTag = 2;
const noTranslation = 0;
const eventNames = [
  "onGestureHandlerReanimatedStateChange",
  "onGestureHandlerReanimatedEvent",
  "onGestureHandlerReanimatedTouchEvent",
];

const exportedFunction = async (modulePath: string, name: string): Promise<Callable> => {
  const exported: unknown = Reflect.get(await import(modulePath), name);

  if (typeof exported !== "function") {
    throw new TypeError(`${modulePath} must export the function ${name}`);
  }

  return (...callArguments) => Reflect.apply(exported, null, callArguments);
};

const renderedDispatchers = async (
  handlerTags: readonly number[],
  eventHandler: unknown,
): Promise<{ readonly handlerTags: unknown; readonly dispatch: (eventName: string, event: unknown) => unknown }> => {
  const rendered: unknown = (await exportedFunction(detectorPath, "ReanimatedNativeDetector"))({
    handlerTags,
    onGestureHandlerReanimatedEvent: eventHandler,
  });
  const props: unknown = typeof rendered === "object" && rendered !== null ? Reflect.get(rendered, "props") : null;

  if (typeof props !== "object" || props === null) {
    throw new TypeError("the detector must render an element");
  }

  return {
    dispatch: (eventName, event) => Reflect.apply(Reflect.get(props, eventName), null, [event]),
    handlerTags: Reflect.get(props, "handlerTags"),
  };
};

const activationEvent = (): { nativeEvent: { oldState: number; state: number } } => ({
  nativeEvent: { oldState: stateBegan, state: stateActive },
});

it("schedules each event onto the UI runtime flat and named, as it was when the engine sent it", async () => {
  const worklet = vi.fn();
  const detector = await renderedDispatchers([], { workletEventHandler: { worklet } });
  for (const eventName of eventNames) {
    const event = activationEvent();
    detector.dispatch(eventName, event);
    Reflect.deleteProperty(event.nativeEvent, "oldState");
  }

  expect({ handlerTags: detector.handlerTags, scheduled: worklet.mock.calls }).toStrictEqual({
    handlerTags: [],
    scheduled: eventNames.map((eventName) => [{ ...activationEvent().nativeEvent, eventName }]),
  });
});

const fieldOf = (value: unknown, name: string): unknown =>
  typeof value === "object" && value !== null ? Reflect.get(value, name) : null;

interface Translated {
  readonly handlerData: { readonly translationX: number };
}

const withChangeX = (event: Translated, lastUpdate?: Translated): unknown => ({
  ...event,
  handlerData: {
    ...event.handlerData,
    changeX: event.handlerData.translationX - (lastUpdate?.handlerData.translationX ?? noTranslation),
  },
});

const panUpdateSender = async (handlerTag: number, changes: unknown[]): Promise<(translationX: number) => unknown> => {
  const createEventHandler = await exportedFunction(eventHandlerHookPath, "useReanimatedEventHandler");
  const onUpdate = (event: unknown): void => {
    changes.push({ changeX: fieldOf(event, "changeX"), handlerTag: fieldOf(event, "handlerTag") });
  };
  const detector = await renderedDispatchers(
    [handlerTag],
    createEventHandler(handlerTag, { onUpdate }, null, withChangeX),
  );

  return (translationX) =>
    detector.dispatch("onGestureHandlerReanimatedEvent", {
      nativeEvent: { handlerData: { translationX }, handlerTag },
    });
};

it("keeps each gesture's last update apart, so simultaneous gestures report their own changes", async () => {
  const changes: unknown[] = [];
  const first = await panUpdateSender(firstPanTag, changes);
  const second = await panUpdateSender(secondPanTag, changes);
  const updates = [
    { send: first, translationX: 10 },
    { send: second, translationX: 100 },
    { send: first, translationX: 15 },
    { send: second, translationX: 130 },
  ];
  for (const { send, translationX } of updates) {
    send(translationX);
  }

  expect(changes).toStrictEqual([
    { changeX: 10, handlerTag: firstPanTag },
    { changeX: 100, handlerTag: secondPanTag },
    { changeX: 5, handlerTag: firstPanTag },
    { changeX: 30, handlerTag: secondPanTag },
  ]);
});
