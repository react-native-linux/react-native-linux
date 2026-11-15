import { expect, it, vi } from "vitest";

const sourceDirectory = "../upstream/packages/react-native-gesture-handler/src";
const modulePath = `${sourceDirectory}/RNGestureHandlerModule.linux.ts`;

vi.mock("react-native", () => ({ Platform: { OS: "linux" } }));
vi.mock("../upstream/packages/react-native-gesture-handler/src/handlers/gestures/reanimatedWrapper.ts", () => ({
  Worklets: {
    scheduleOnRN: (scheduled: Method, ...scheduledArguments: unknown[]): unknown => scheduled(...scheduledArguments),
  },
}));
vi.mock("../upstream/packages/react-native-gesture-handler/src/RNGestureHandlerModule.ts", () => import(modulePath));

const routerPath = `${sourceDirectory}/web/tools/LinuxPointerRouter.ts`;
const nativeProxyPath = `${sourceDirectory}/v3/NativeProxy.linux.ts`;
const jsFunctionNewApi = 4;
const stateBegan = 2;
const stateFailed = 1;
const stateCancelled = 3;
const stateActive = 4;
const stateEnd = 5;
const moduleIdWithoutNativeRegistry = -1;
const clickedHandlerTag = 1;
const detachedHandlerTag = 2;
const unknownHandlerTag = 3;
const refusedHandlerTag = 4;
const configuredHandlerTag = 5;
const draggedHandlerTag = 6;
const contentsHandlerTag = 7;
const anchoredHandlerTag = 8;
const toastHandlerTag = 9;
const scrollHandlerTag = 10;
const siblingHandlerTag = 11;
const firstPointerId = 1;
const secondPointerId = 2;
const dragDistance = 50;
const movesBeforeDetach = 1;
const rect = { bottom: 100, left: 0, right: 100, top: 0 };

interface TestRect {
  readonly bottom: number;
  readonly left: number;
  readonly right: number;
  readonly top: number;
}

interface TestElement {
  readonly children?: readonly TestElement[];
  readonly contains: (other: unknown) => boolean;
  readonly getBoundingClientRect: () => TestRect;
}

const createElement = (): TestElement => {
  const created: TestElement = { contains: (other) => other === created, getBoundingClientRect: () => rect };

  return created;
};

const element = createElement();
const draggedElement = createElement();
const pointerEvent = { nativeEvent: { clientX: 10, clientY: 10, pointerId: 1 }, target: element, timeStamp: 1 };
const dragStart = { ...pointerEvent, target: draggedElement };
const dragEnd = {
  ...dragStart,
  nativeEvent: { ...pointerEvent.nativeEvent, clientX: pointerEvent.nativeEvent.clientX + dragDistance },
  timeStamp: 2,
};

type Method = (...methodArguments: unknown[]) => unknown;
type StateChangeMock = ReturnType<typeof vi.fn>;

const propertyOf = (owner: unknown, name: string): unknown =>
  typeof owner === "object" && owner !== null ? Reflect.get(owner, name) : null;

const methodOf = (owner: unknown, name: string): Method => {
  const method = propertyOf(owner, name);

  if (typeof method !== "function") {
    throw new TypeError(`${name} must be a function`);
  }

  return (...methodArguments) => Reflect.apply(method, owner, methodArguments);
};

const loadGestureModule = async (): Promise<unknown> => propertyOf(await import(modulePath), "default");

const loadRoute = async (): Promise<Method> => methodOf(await import(routerPath), "routeLinuxPointerEvent");

const loadNativeProxy = async (): Promise<unknown> => propertyOf(await import(nativeProxyPath), "NativeProxy");

const statesOf = (stateChange: StateChangeMock): unknown[] =>
  stateChange.mock.calls.map(([event]: unknown[]) => propertyOf(propertyOf(event, "nativeEvent"), "state"));

const attachNativeRecognizer = (gestureModule: unknown, handlerTag: number, stateChange: StateChangeMock): void => {
  const propsRef = { current: { onGestureHandlerEvent: vi.fn(), onGestureHandlerStateChange: stateChange } };

  methodOf(gestureModule, "createGestureHandler")("NativeViewGestureHandler", handlerTag, {});
  methodOf(gestureModule, "attachGestureHandler")(handlerTag, element, jsFunctionNewApi, propsRef);
  methodOf(gestureModule, "updateGestureHandlerConfig")(handlerTag, { shouldCancelWhenOutside: true });
  methodOf(gestureModule, "configureRelations")(handlerTag, { simultaneousHandlers: [], waitFor: [] });
};

it("begins a Native recognizer on a Linux view and fails it on a click, through the 3.x module contract", async () => {
  const gestureModule = await loadGestureModule();
  const route = await loadRoute();
  const stateChange = vi.fn();

  attachNativeRecognizer(gestureModule, clickedHandlerTag, stateChange);
  route("pointerdown", pointerEvent);
  route("pointerup", pointerEvent);

  expect(statesOf(stateChange)).toStrictEqual([stateBegan, stateFailed]);
});

it("routes nothing to a detached recognizer and forgets a dropped one", async () => {
  const gestureModule = await loadGestureModule();
  const route = await loadRoute();
  const stateChange = vi.fn();
  attachNativeRecognizer(gestureModule, detachedHandlerTag, stateChange);
  methodOf(gestureModule, "detachGestureHandler")(detachedHandlerTag);
  route("pointerdown", pointerEvent);
  methodOf(gestureModule, "dropGestureHandler")(detachedHandlerTag);

  expect(stateChange).not.toHaveBeenCalled();
  expect(() => methodOf(gestureModule, "getGestureHandlerNode")(detachedHandlerTag)).toThrow(
    `No handler for tag ${detachedHandlerTag}`,
  );
});

it("answers the native module's queries without a native gesture registry", async () => {
  const gestureModule = await loadGestureModule();

  expect(methodOf(gestureModule, "configureRelations")(unknownHandlerTag, {})).toBeUndefined();
  expect(methodOf(gestureModule, "getModuleId")()).toBe(moduleIdWithoutNativeRegistry);
  expect(methodOf(gestureModule, "installUIRuntimeBindings")()).toBe(true);
  expect(methodOf(gestureModule, "flushOperations")()).toBeUndefined();
});

it("names the platform when asked for a recognizer the engine does not have", async () => {
  const gestureModule = await loadGestureModule();

  expect(() =>
    methodOf(gestureModule, "createGestureHandler")("ForceTouchGestureHandler", refusedHandlerTag, {}),
  ).toThrow("ForceTouchGestureHandler is not supported on linux");
});

it("applies a config update the UI runtime defers to the module, and ignores one for a dropped handler", async () => {
  const gestureModule = await loadGestureModule();
  const updateFromUIRuntime = methodOf(await loadNativeProxy(), "updateGestureHandlerConfig");

  methodOf(gestureModule, "createGestureHandler")("PanGestureHandler", configuredHandlerTag, {});
  updateFromUIRuntime(configuredHandlerTag, { enabled: false });
  const handler = methodOf(gestureModule, "getGestureHandlerNode")(configuredHandlerTag);
  methodOf(gestureModule, "dropGestureHandler")(configuredHandlerTag);

  expect(propertyOf(handler, "enabled")).toBe(false);
  expect(() => updateFromUIRuntime(configuredHandlerTag, { enabled: true })).not.toThrow();
});

const attachPan = (gestureModule: unknown, stateChange: StateChangeMock): void => {
  const propsRef = { current: { onGestureHandlerEvent: vi.fn(), onGestureHandlerStateChange: stateChange } };

  methodOf(gestureModule, "attachGestureHandler")(draggedHandlerTag, draggedElement, jsFunctionNewApi, propsRef);
};

const recordPointerCallbacks = (handler: unknown): StateChangeMock => {
  const pointerCallbacks = vi.fn();

  if (typeof handler !== "object" || handler === null) {
    throw new TypeError("the handler must be an object");
  }

  for (const name of ["onPointerMove", "onPointerUp", "onPointerCancel"]) {
    const recorded = methodOf(handler, name);
    Reflect.set(handler, name, (...recordedArguments: unknown[]): unknown => {
      pointerCallbacks(name);
      return recorded(...recordedArguments);
    });
  }

  return pointerCallbacks;
};

const drag = (route: Method, ...types: readonly string[]): void => {
  for (const type of types) {
    route(type, type === "pointerdown" ? dragStart : dragEnd);
  }
};

const startDragThenDetach = (gestureModule: unknown, route: Method, stateChange: StateChangeMock): void => {
  attachPan(gestureModule, stateChange);
  drag(route, "pointerdown", "pointermove");
  methodOf(gestureModule, "detachGestureHandler")(draggedHandlerTag);
  drag(route, "pointermove", "pointercancel");
};

it("stops a drag at detach, and takes the next one after the handler is attached again", async () => {
  const gestureModule = await loadGestureModule();
  const route = await loadRoute();
  const [firstStateChange, secondStateChange] = [vi.fn(), vi.fn()];
  methodOf(gestureModule, "createGestureHandler")("PanGestureHandler", draggedHandlerTag, {});
  const pointerCallbacks = recordPointerCallbacks(methodOf(gestureModule, "getGestureHandlerNode")(draggedHandlerTag));
  startDragThenDetach(gestureModule, route, firstStateChange);
  const pointerCallbacksAfterDetach = pointerCallbacks.mock.calls.length;
  attachPan(gestureModule, secondStateChange);
  drag(route, "pointerdown", "pointermove", "pointerup");

  expect({
    first: statesOf(firstStateChange),
    pointerCallbacksAfterDetach,
    second: statesOf(secondStateChange),
  }).toStrictEqual({
    first: [stateBegan, stateActive, stateCancelled],
    pointerCallbacksAfterDetach: movesBeforeDetach,
    second: [stateBegan, stateActive, stateEnd],
  });
});

const noBox = { bottom: 0, left: 0, right: 0, top: 0 };
const boxedChild: TestElement = { contains: (other) => other === boxedChild, getBoundingClientRect: () => rect };
const emptyChild: TestElement = { contains: () => false, getBoundingClientRect: () => noBox };
const zeroSizeParent = (): TestElement => {
  const parent: TestElement = {
    children: [emptyChild, boxedChild],
    contains: (other) => other === parent || boxedChild.contains(other),
    getBoundingClientRect: () => noBox,
  };

  return parent;
};

type RoutedPointer = readonly [string, unknown];

interface HandlerTarget {
  readonly config?: object;
  readonly element: TestElement;
  readonly handlerTag: number;
  readonly hostDetector?: unknown;
  readonly name?: string;
}

const routeToHandler = async (
  { config = {}, element: attached, handlerTag, hostDetector = null, name = "PanGestureHandler" }: HandlerTarget,
  pointers: readonly RoutedPointer[],
): Promise<StateChangeMock> => {
  const gestureModule = await loadGestureModule();
  const route = await loadRoute();
  const stateChange = vi.fn();
  const propsRef = { current: { onGestureHandlerEvent: vi.fn(), onGestureHandlerStateChange: stateChange } };
  methodOf(gestureModule, "createGestureHandler")(name, handlerTag, {});
  methodOf(gestureModule, "attachGestureHandler")(handlerTag, attached, jsFunctionNewApi, propsRef, hostDetector);
  methodOf(gestureModule, "updateGestureHandlerConfig")(handlerTag, config);
  for (const [type, event] of pointers) {
    route(type, event);
  }

  return stateChange;
};

const childDrag: readonly RoutedPointer[] = [
  ["pointerdown", { ...dragStart, target: boxedChild }],
  ["pointermove", { ...dragEnd, target: boxedChild }],
  ["pointerup", { ...dragEnd, target: boxedChild }],
];

it("bounds a display: contents detector by its children, so a drag on its child is recognized", async () => {
  const contents = zeroSizeParent();
  const stateChange = await routeToHandler(
    { element: contents, handlerTag: contentsHandlerTag, hostDetector: { current: contents } },
    childDrag,
  );

  expect(statesOf(stateChange)).toStrictEqual([stateBegan, stateActive, stateEnd]);
});

it("keeps a zero-size view's own bounds when it is not its detector, so a drag on its child is not recognized", async () => {
  const stateChange = await routeToHandler({ element: zeroSizeParent(), handlerTag: anchoredHandlerTag }, childDrag);

  expect(stateChange).not.toHaveBeenCalled();
});

const toastElement = createElement();
const toastConfig = { activeOffsetXEnd: 10, activeOffsetXStart: -10, failOffsetYEnd: 12, failOffsetYStart: -12 };
const pressPoint = { clientX: 10, clientY: 10 };
const verticalPoint = { clientX: 11, clientY: 40 };
const crossingPoint = { clientX: 40, clientY: 11 };
const releasePoint = { clientX: 80, clientY: 13 };
const pointerOn =
  (target: TestElement, pointerId: number) =>
  (type: string, point: typeof pressPoint): RoutedPointer => [
    type,
    { nativeEvent: { ...point, pointerId }, target, timeStamp: 1 },
  ];
const toastPointer = pointerOn(toastElement, firstPointerId);

it("begins a press that arrives in the same batch as the failure before it, as a busy JavaScript thread receives them", async () => {
  const stateChange = await routeToHandler(
    { config: toastConfig, element: toastElement, handlerTag: toastHandlerTag },
    [
      toastPointer("pointerdown", pressPoint),
      toastPointer("pointermove", verticalPoint),
      toastPointer("pointerup", verticalPoint),
      toastPointer("pointerdown", pressPoint),
      toastPointer("pointermove", crossingPoint),
      toastPointer("pointermove", releasePoint),
      toastPointer("pointerup", releasePoint),
    ],
  );

  expect(statesOf(stateChange)).toStrictEqual([stateBegan, stateFailed, stateBegan, stateActive, stateEnd]);
});

const scrollElement = createElement();
const scrollPointer = pointerOn(scrollElement, firstPointerId);
const siblingElement = createElement();
const siblingPointer = pointerOn(siblingElement, secondPointerId);

it("activates a pan after an active Native recognizer is cancelled in the same batch, as a scroll view cancelled mid-drag is", async () => {
  await routeToHandler({ element: scrollElement, handlerTag: scrollHandlerTag, name: "NativeViewGestureHandler" }, []);
  const stateChange = await routeToHandler({ element: siblingElement, handlerTag: siblingHandlerTag }, [
    scrollPointer("pointerdown", pressPoint),
    scrollPointer("pointermove", releasePoint),
    siblingPointer("pointerdown", pressPoint),
    scrollPointer("pointercancel", releasePoint),
    siblingPointer("pointermove", releasePoint),
    siblingPointer("pointerup", releasePoint),
  ]);

  expect(statesOf(stateChange)).toStrictEqual([stateBegan, stateActive, stateEnd]);
});
